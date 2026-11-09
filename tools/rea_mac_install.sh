#!/bin/sh
# One-command install of REA 6.5.0 (github.com/morluto/rea) on your Mac.
#   tools/rea_mac_install.sh          install + check
#   tools/rea_mac_install.sh --mcp    also SHOW (dry run, changes nothing) how REA would be
#                                     added to Claude Desktop; you approve the real step later
cd "$(dirname "$0")/.." || exit 1
DEST="$HOME/Tools/rea-6.5.0"; BIN="$HOME/Tools/bin"
command -v node >/dev/null || { echo "Node is missing: brew install node"; exit 1; }
echo "==> 1/3 Installing REA 6.5.0 into $DEST (lockfile pinned, no install scripts)"
mkdir -p "$DEST" "$BIN"
cp tools/rea/package.json tools/rea/package-lock.json "$DEST/"
(cd "$DEST" && npm ci --ignore-scripts --no-audit --no-fund) || { echo "npm ci failed; copy the output back to Computer"; exit 1; }
echo "==> 2/3 Adding the 'rea' command"
cat > "$BIN/rea" <<EOS
#!/bin/sh
[ "\$1" = "update" ] && { echo "REA is pinned to 6.5.0 (tools/rea). Update it through the repo."; exit 2; }
exec node "$DEST/node_modules/rea-agents/scripts/rea.mjs" "\$@"
EOS
chmod +x "$BIN/rea"
case ":$PATH:" in *":$BIN:"*) ;; *) echo "export PATH=\"\$HOME/Tools/bin:\$PATH\"" >> "$HOME/.zprofile"; echo "    added ~/Tools/bin to PATH in ~/.zprofile (open a new Terminal tab)";; esac
echo "==> 3/3 Check"
V="$("$BIN/rea" --version 2>&1)"; echo "    rea --version: $V"
case "$V" in *6.5.0*) echo "  PASS  REA 6.5.0 installed";; *) echo "  FAIL  unexpected version";; esac
"$BIN/rea" doctor --json > "$DEST/doctor.json" 2>&1 && echo "  PASS  rea doctor ran (report: $DEST/doctor.json)" || echo "  note  rea doctor reported missing engines (normal: Ghidra/Hopper are optional). Report: $DEST/doctor.json"
if [ "${1:-}" = "--mcp" ]; then
  echo "==> Dry run: what REA setup WOULD change for Claude Desktop (nothing is written)"
  "$BIN/rea" setup --client claude_desktop --dry-run --json
fi
echo "Done. Try:  rea inspect-artifact \"\$HOME/Downloads/TrioUpdaterInstaller_v2.0.2.dmg\" --format json"
