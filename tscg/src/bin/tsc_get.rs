use std::io::prelude::*;
use std::io::ErrorKind;
use std::os::unix::net::UnixStream;

struct Config {
    name: String,
}

fn main() {
    let path = std::env::var("GENIE_PATH")
        .ok()
        .expect("GENIE_PATH env var is not set")
        .to_string();

    let cookie = std::env::var("GENIE_COOKIE").expect("GENIE_COOKIE is not set");

    let config = Config {
        name: "tsc".to_string(),
    };

    let (sockname, _) = genie::find(&path, &config.name, &None)
        .expect(&format!("no genie found by name {}", config.name));

    let mut stream = UnixStream::connect(sockname).expect("error connecting to socket");
    stream
        .write_all(format!("get\n{}\n", cookie).as_bytes())
        .expect("error writing to stream");

    let mut buffer = [0; 8192];
    loop {
        match stream.read(&mut buffer) {
            Ok(0) => break,
            Ok(bytes) => std::io::stdout()
                .write_all(&buffer[..bytes])
                .expect("error writing to stream"),
            Err(err) if err.kind() == ErrorKind::Interrupted => continue,
            Err(err) => {
                eprintln!("error reading from stream: {}", err);
                break;
            }
        }
    }
}
