use std::{
    collections::HashMap,
    fs::remove_file,
    process::Output,
    sync::{Arc, Mutex},
};

use tokio::{
    net::{UnixListener, UnixStream},
    prelude::*,
    process::Command,
    stream::StreamExt,
};

use conf::{configure, Config};

pub enum Request {
    Poll(GenieCookie),
    Get(GenieCookie),
    Exit,
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

struct WatchState {
    fingers: HashMap<GenieCookie, (u32, Arc<Output>)>,
    latest: Option<(u32, Arc<Output>)>,
}

impl WatchState {
    fn new() -> WatchState {
        return WatchState {
            fingers: HashMap::new(),
            latest: None,
        };
    }

    fn update(&mut self, iteration: u32, output: &Output) {
        self.latest = Some((iteration, Arc::new(output.clone())))
    }

    fn poll(&mut self, cookie: GenieCookie) -> Option<Arc<Output>> {
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

    fn get(&self, cookie: GenieCookie) -> Option<Arc<Output>> {
        match &self.fingers.get(&cookie) {
            None => match &self.latest {
                None => None,
                Some((_, output)) => Some(output.clone()),
            },
            Some((_, output)) => Some(output.clone()),
        }
    }
}

async fn send_output_to_stream(stream: &mut UnixStream, output: &Output, beep: bool) {
    if beep {
        match output.status.code() {
            Some(0) => (),
            None | Some(_) => stream.write_all(&[0o007]).await.unwrap(),
        }
    }

    if output.stderr.len() > 0 {
        stream.write_all(&output.stderr).await.unwrap()
    }

    if output.stdout.len() > 0 {
        stream.write_all(&output.stdout).await.unwrap()
    }
}

mod conf {
    use clap::{App, AppSettings, Arg};

    pub struct Config {
        pub genie_dir: String,
        pub name: String,
        pub command: String,
        pub interval: u64, // milliseconds
        pub beep: bool,
        pub logfile: Option<String>,
    }

    pub fn configure() -> Config {
        let matches = App::new("watchg")
            .setting(AppSettings::TrailingVarArg)
            .author("David L. L. Thomas <davidleothomas@gmail.com>")
            .about("execute a program periodically, make its output available as a genie")
            .arg(
                Arg::with_name("interval")
                    .short("n")
                    .long("interval")
                    .takes_value(true)
                    .value_name("seconds"),
            )
            .arg(Arg::with_name("beep").short("b").long("beep"))
            .arg(Arg::from_usage("<cmd>... 'command to run'"))
            .arg(
                Arg::with_name("logfile")
                    .short("l")
                    .long("logfile")
                    .takes_value(true)
                    .value_name("file"),
            )
            .get_matches();

        let name = "watch".to_string();
        let path = std::env::var("GENIE_PATH")
            .ok()
            .expect("GENIE_PATH env var is not set");
        let genie_dir = path.split(":").next().unwrap().to_string();

        let command = matches
            .values_of("cmd")
            .unwrap()
            .collect::<Vec<&str>>()
            .join(" ");

        let interval = match matches.value_of("interval") {
            None => 2.0,
            Some(string) => string.parse().unwrap(),
        };
        let interval = (1000.0 * interval) as u64;

        let beep = matches.is_present("beep");

        let logfile = matches.value_of("logfile").map(String::from);

        Config {
            genie_dir,
            name,
            command,
            interval,
            beep,
            logfile,
        }
    }
}

fn main() {
    let Config {
        genie_dir,
        name,
        command,
        interval,
        beep,
        logfile,
    } = configure();
    let state = Arc::new(Mutex::new(WatchState::new()));

    let daemonize = daemonize::Daemonize::new().working_directory(".");

    let daemonize = match logfile {
        Some(logfile) => {
            let logfile = std::fs::OpenOptions::new()
                .append(true)
                .create(true)
                .open(logfile)
                .expect("unable to open logfile");

            let stdout = logfile.try_clone().expect("unable to clone logfile handle");
            let stderr = logfile;

            daemonize.stdout(stdout).stderr(stderr)
        }
        None => daemonize,
    };

    daemonize.start().expect("failed to daemonize");

    let socket_path = { format!("{}/{}.{:x}.sock", genie_dir, name, std::process::id()) };

    tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()
        .unwrap()
        .block_on(async {
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
                                    Err(err) if err.kind() == std::io::ErrorKind::Interrupted => {
                                        continue
                                    }
                                    Err(_err) => break 'stream,
                                }

                                loop {
                                    match parse::request(&buffer[..nbytes]) {
                                        Ok((_, request)) => {
                                            match request {
                                                Request::Poll(cookie) => {
                                                    let output =
                                                        state.lock().unwrap().poll(cookie).clone();
                                                    if let Some(output) = output {
                                                        send_output_to_stream(
                                                            &mut stream,
                                                            &output,
                                                            beep,
                                                        )
                                                        .await
                                                    }
                                                }
                                                Request::Get(cookie) => {
                                                    let output =
                                                        state.lock().unwrap().get(cookie).clone();
                                                    if let Some(output) = output {
                                                        send_output_to_stream(
                                                            &mut stream,
                                                            &output,
                                                            false,
                                                        )
                                                        .await
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

            let mut iteration = 0;

            loop {
                let output: Output = Command::new("bash")
                    .arg("-c")
                    .arg(command.clone())
                    .output()
                    .await
                    .unwrap();

                state.lock().unwrap().update(iteration, &output);

                std::thread::sleep(std::time::Duration::from_millis(interval));
                iteration += 1;
            }
        });
}
