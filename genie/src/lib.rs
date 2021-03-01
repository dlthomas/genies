pub const SOCKNAME_PATTERN: &str = r"([a-z0-9]+)\.([a-z0-9]+)\.sock";

pub fn nth(path: &String, n: usize) -> Option<String> {
    path.split(':').nth(n).map(String::from)
}

pub fn find(
    path: &String,
    name: &String,
    num: &Option<String>,
) -> Option<(std::path::PathBuf, usize)> {
    let re = regex::Regex::new(SOCKNAME_PATTERN).unwrap();
    for (i, dir) in path.split(':').enumerate() {
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

            let sockname = entry
                .file_name()
                .into_string()
                .expect("unconvertable file name");
            let captures = match re.captures(&sockname) {
                None => continue,
                Some(captures) => captures,
            };

            if *name == captures[1].to_string()
                && (num.is_none() || *num == Some(captures[2].to_string()))
            {
                return Some((entry.path(), i));
            }
        }
    }

    None
}
