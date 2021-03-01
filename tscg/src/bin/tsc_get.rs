use std::io::prelude::*;
use std::io::ErrorKind;
use std::os::unix::net::UnixStream;

struct Config {
    name: String,
}

fn main() {
    let genies = std::env::var("GENIES").expect("missing GENIE list");
    let cookie = std::env::var("GENIE_COOKIE").expect("missing GENIE_COOKIE");

    let config = Config {
        name: "tsc".to_string(),
    };

    let regex = regex::Regex::new(":?([^,]+),([^:]+)(.*)").expect("error parsing regex");

    let mut genies: &str = genies.as_str();
    let mut found = false;
    while let Some(captures) = regex.captures(genies) {
        let name = captures.get(1).unwrap().as_str();
        let socket = captures.get(2).unwrap().as_str();
        genies = captures.get(3).unwrap().as_str();

        if name == config.name {
            found = true;
            let mut stream = UnixStream::connect(socket).expect("error connecting to socket");
            stream.write_all(format!("get\n{}\n", cookie).as_bytes()).expect("error writing to stream");
            
            let mut buffer = [0; 8192];
            loop {
                match stream.read(&mut buffer) {
                    Ok(0) => break,
                    Ok(bytes) => std::io::stdout().write_all(&buffer[..bytes]).expect("error writing to stream"),
                    Err(err) if err.kind() == ErrorKind::Interrupted => continue,
                    Err(err) => {
                        eprintln!("error reading from stream: {}", err);
                        break
                    }
                }
            }

            break
        }
    }

    if !found {
        eprintln!("no genie found by name {}", config.name);
        std::process::exit(1)
    }
}
