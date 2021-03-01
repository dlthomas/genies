fn main() {
    let path = std::env::var("GENIE_PATH").expect("GENIE_PATH env var is not set");
    for genie in std::env::args().skip(1) {
        let mut split = genie.split(".");
        let name = split.next().unwrap().to_string();
        let num = split.next().map(String::from);

        match genie::find(&path, &name, &num) {
            Some((src, n)) => match genie::nth(&path, n + 1) {
                Some(dst) => {
                    let filename = src.file_name().expect("unable to get file name");
                    let mut dst = std::path::PathBuf::from(dst);
                    dst.push(filename);

                    std::fs::rename(src, dst).expect("failed to move socket");
                }
                None => eprintln!("path exhausted, cannot promote {}", &name),
            },
            None => eprintln!("genie not found in path"),
        }
    }
}
