# Security policy

Report vulnerabilities privately to the repository owner via GitHub private
vulnerability reporting (Security tab -> Report a vulnerability). Do not open
public issues for undisclosed vulnerabilities.

Cryptographic operations use libsodium exclusively; secret material is held in
zeroizing buffers and never logged. All untrusted input (XDR, JSON, TOML, URIs)
is parsed with strict, bounded decoders — a decode bug is a security bug; please
report it.
