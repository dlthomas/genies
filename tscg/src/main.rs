use std::{
    collections::HashMap,
    fs::remove_file,
    process::Stdio,
    sync::{Arc, Mutex},
};

use tokio::{
    io::{AsyncBufReadExt, BufReader},
    net::{UnixListener, UnixStream},
    prelude::*,
    process::{Child, Command},
    stream::StreamExt,
};

use conf::{configure, Config};

pub enum Request {
    Poll(GenieCookie),
    Get(GenieCookie),
    Exit,
}

fn setpgrp() -> io::Result<()> {
    let ret = unsafe {
        libc::setpgid(0, 0)
    };
    if ret < 0 {
        Err(io::Error::last_os_error())
    } else {
        Ok(())
    }
}

mod parse {
    use super::{GenieCookie, Request};
    use nom::{
        branch::alt,
        bytes::streaming::tag,
        character::streaming::{alphanumeric1, newline},
        IResult,
    };

    fn genie_cookie(i: &[u8]) -> IResult<&[u8], GenieCookie> {
        let (i, cookie) = alphanumeric1(i)?;
        match std::str::from_utf8(cookie) {
            Ok(cookie) => Ok((i, GenieCookie(cookie.to_string()))),
            Err(_) => panic!(
                "we were already told it was alphanumeric, how does it have incomplete utf8?"
            ),
        }
    }

    fn poll_request(i: &[u8]) -> IResult<&[u8], Request> {
        let (i, _) = tag("poll\n")(i)?;
        let (i, cookie) = genie_cookie(i)?;
        let (i, _) = newline(i)?;
        Ok((i, Request::Poll(cookie)))
    }

    fn get_request(i: &[u8]) -> IResult<&[u8], Request> {
        let (i, _) = tag("get\n")(i)?;
        let (i, cookie) = genie_cookie(i)?;
        let (i, _) = newline(i)?;
        Ok((i, Request::Get(cookie)))
    }

    fn exit_request(i: &[u8]) -> IResult<&[u8], Request> {
        let (i, _) = tag("exit\n")(i)?;
        Ok((i, Request::Exit))
    }

    pub fn request(i: &[u8]) -> IResult<&[u8], Request> {
        alt((poll_request, get_request, exit_request))(i)
    }
}

#[derive(PartialEq, Eq, Hash)]
pub struct GenieCookie(String);

struct TscState {
    fingers: HashMap<GenieCookie, (u32, Option<Arc<(u16, String)>>)>,
    latest: Option<(u32, Option<Arc<(u16, String)>>)>,
}

impl TscState {
    fn new() -> TscState {
        return TscState {
            fingers: HashMap::new(),
            latest: None,
        };
    }

    fn update(&mut self, iteration: u32, output: Option<(u16, String)>) {
        self.latest = Some((iteration, output.map(Arc::new)));
    }

    fn poll(&mut self, cookie: GenieCookie) -> Option<Option<Arc<(u16, String)>>> {
        match &self.latest {
            None => None,
            Some((iteration, output)) => {
                match &self.fingers.insert(cookie, (*iteration, output.clone())) {
                    None => Some(output.clone()),
                    Some((last_polled, _)) => {
                        if iteration == last_polled {
                            None
                        } else {
                            Some(output.clone())
                        }
                    }
                }
            }
        }
    }

    fn get(&self, cookie: GenieCookie) -> Option<Option<Arc<(u16, String)>>> {
        match &self.fingers.get(&cookie) {
            None => match &self.latest {
                None => None,
                Some((_, output)) => Some(output.clone()),
            },
            Some((_, output)) => Some(output.clone()),
        }
    }
}

async fn send_error_count_to_stream(stream: &mut UnixStream, output: &Option<Arc<(u16, String)>>) {
    match output {
        Some(output) => {
            let msg = format!("{} errors", output.0);
            stream.write_all(msg.as_bytes()).await.unwrap()
        }
        None => {}
    }
}

async fn send_output_to_stream(stream: &mut UnixStream, output: &Option<Arc<(u16, String)>>) {
    match output {
        Some(output) => {
            if output.1.len() > 0 {
                stream.write_all(output.1.as_bytes()).await.unwrap()
            } else {
                stream
                    .write_all("... no output ...".as_bytes())
                    .await
                    .unwrap()
            }
        }
        None => stream.write_all("compiling...".as_bytes()).await.unwrap(),
    }
}

mod conf {
    use clap::{App, AppSettings, Arg};

    pub struct Config {
        pub name: String,
        pub socket_path: String,
        pub args: Vec<String>,
    }

    pub fn configure() -> Config {
        let matches = App::new("tscg")
            .setting(AppSettings::TrailingVarArg)
            .author("David L. L. Thomas <davidleothomas@gmail.com>")
            .arg(Arg::from_usage("[arg]... 'args to pass to tsc'"))
            .get_matches();

        let name = "tsc".to_string();
        let genie_dir = ".";
        let socket_path = { format!("{}/.{}.{:x}.sock", genie_dir, name, rand::random::<u32>()) };

        let args = matches
            .values_of("arg")
            .unwrap_or_default()
            .map(ToString::to_string)
            .collect();

        Config {
            name,
            socket_path,
            args,
        }
    }
}

#[tokio::main]
async fn main() {
    let Config {
        name,
        socket_path,
        args,
    } = configure();
    let state = Arc::new(Mutex::new(TscState::new()));
    print!("{}\n{}\n", name, socket_path);

    setpgrp().expect("failed to set process group for genie");

    {
        let state = state.clone();
        let mut listener = UnixListener::bind(&socket_path).unwrap();
        tokio::spawn(async move {
            'top: loop {
                if let Some(Ok(mut stream)) = listener.next().await {
                    let mut nbytes = 0;
                    let mut buffer: [u8; 8192] = [0; 8192];
                    'stream: loop {
                        match stream.read(&mut buffer[nbytes..]).await {
                            Ok(0) => break,
                            Ok(length) => nbytes += length,
                            Err(err) if err.kind() == std::io::ErrorKind::Interrupted => continue,
                            Err(_err) => break 'stream,
                        }

                        loop {
                            match parse::request(&buffer[..nbytes]) {
                                Ok((_, request)) => {
                                    match request {
                                        Request::Poll(cookie) => {
                                            let output = state.lock().unwrap().poll(cookie).clone();
                                            if let Some(output) = output {
                                                send_error_count_to_stream(&mut stream, &output).await
                                            }
                                        }
                                        Request::Get(cookie) => {
                                            let output = state.lock().unwrap().get(cookie).clone();
                                            if let Some(output) = output {
                                                send_output_to_stream(&mut stream, &output).await
                                            }
                                        }
                                        Request::Exit => break 'top,
                                    }
                                    break 'stream;
                                }
                                Err(nom::Err::Incomplete(_)) => continue 'stream,
                                Err(_) => break 'stream,
                            }
                        }
                    }
                }
            }

            match remove_file(socket_path) {
                Err(_) => (),
                Ok(_) => (),
            }

            std::process::exit(0)
        });
    }

    let mut iteration: u32 = 0;

    let tsc: Child = args
        .into_iter()
        .fold(Command::new("tsc").arg("--watch"), |tsc, arg| tsc.arg(arg))
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .kill_on_drop(true)
        .spawn()
        .expect("faild to spawn tsc process");

    let stdout = tsc
        .stdout
        .expect("stdout from child process was unavailable");
    let stdout = BufReader::new(stdout);
    let stdout = stdout.lines().map(Result::unwrap).map(Ok);

    let stderr = tsc
        .stderr
        .expect("stderr from child process was unavailable");
    let stderr = BufReader::new(stderr);
    let stderr = stderr.lines().map(Result::unwrap).map(Err);

    let mut input = stdout.merge(stderr);

    let mut output = Vec::new();

    let start = regex::Regex::new(
        "Starting compilation in watch mode...\
        |File change detected. Starting incremental compilation...",
    )
    .unwrap();

    let end = regex::Regex::new("Found ([0-9]+) errors. Watching for file changes.").unwrap();

    loop {
        match input.next().await.unwrap() {
            Ok(line) => {
                if start.is_match(&line) {
                    state.lock().unwrap().update(iteration, None)
                }

                output.push(line.to_string());

                if let Some(captures) = end.captures(&line) {
                    let error_count = captures.get(1).unwrap().as_str().parse().unwrap();

                    let output = output.drain(..).collect::<Vec<String>>().join("\n");
                    state.lock().unwrap().update(iteration, Some((error_count, output)))
                }
            }

            Err(line) => {
                output.push(format!("err: {}", line.to_string()));
            }
        }

        iteration += 1;
    }
}
