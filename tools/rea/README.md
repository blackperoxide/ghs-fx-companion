# REA (Reverse Engineer Anything) - pinned local install

`package.json` + `package-lock.json` pin [rea-agents 6.5.0](https://github.com/morluto/rea)
(MIT, Copyright (c) 2026 morluto). Install with `tools/rea_mac_install.sh`; it puts REA in
`~/Tools/rea-6.5.0` and a `rea` command in `~/Tools/bin`. Nothing global, no post-install
scripts. REA itself never installs Ghidra, Java, Binwalk or Hopper.
