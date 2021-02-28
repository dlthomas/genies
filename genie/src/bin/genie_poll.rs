use std::io::prelude::*;
use std::os::unix::net::UnixStream;

fn main() {
    let path = std::env::var("GENIE_PATH")
        .ok()
        .expect("GENIE_PATH env var is not set");
    let path = path.split(':').collect::<Vec<_>>();
    let cookie = std::env::var("GENIE_COOKIE")
        .ok()
        .expect("GENIE_COOKIE env var is not set");

    // TODO: terminfo properly
    let magenta = "\x1b[35m";
    let cyan = "\x1b[36m";
    let white = "\x1b[37m";

    let re = regex::Regex::new(r"([a-z0-9]+)\.([a-z0-9]+)\.sock").unwrap();

    let mut header_printed = false;

    for dir in path {
        let dir = match std::fs::read_dir(dir) {
            Err(_) => continue,
            Ok(dir) => dir,
        };

        for entry in dir {
            let entry = match entry {
                Err(err) => {
                    eprintln!("error retrieving directory entry: {}", err);
                    continue;
                }

                Ok(entry) => entry,
            };

            let name = entry
                .file_name()
                .into_string()
                .expect("unconvertable file name");
            let captures = match re.captures(&name) {
                None => continue,
                Some(captures) => captures,
            };

            let name = captures[1].to_string();
            let pid = captures[2].to_string();

            let path = entry.path();
            let sockname = path.to_str().expect("unconvertable path");
            let mut stream = match UnixStream::connect(&sockname) {
                Err(err) => {
                    match err.kind() {
                        std::io::ErrorKind::ConnectionRefused => {
                            let _ = std::fs::remove_file(&sockname);
                        }

                        _ => eprintln!("error connecting to socket at {}: {}", sockname, err),
                    }
                    continue;
                }

                Ok(stream) => stream,
            };

            if let Err(err) = stream.write_all(format!("poll\n{}\n", &cookie).as_bytes()) {
                eprintln!("{}: error writing to socket: {}", name, err);
                continue;
            }

            let mut response = String::new();
            if let Err(err) = stream.read_to_string(&mut response) {
                eprintln!("{}: error reading from socket: {}", name, err);
                continue;
            }

            if !response.is_empty() {
                if !header_printed {
                    header_printed = true;
                    println!("\n{}~~~{}\n", cyan, white);
                }

                for line in response.lines() {
                    println!("{}{}{}({}): {}", magenta, name, white, pid, line);
                }
            }
        }
    }

    if header_printed {
        println!("")
    }
}
