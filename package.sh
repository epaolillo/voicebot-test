#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

OUTDIR="dist"
BUNDLE_NAME="voicebot-bundle"
STAGE="$OUTDIR/stage"

echo "=== Packaging Voicebot ==="

# Ensure everything is built
if [ ! -f build/voicebot ]; then
    echo "ERROR: build/voicebot not found. Run 'make' first."
    exit 1
fi

if [ ! -f piper/piper ]; then
    echo "ERROR: piper/ not found. Run 'bash setup.sh' first."
    exit 1
fi

if [ -z "$(ls models/*.onnx 2>/dev/null)" ]; then
    echo "ERROR: no model in models/. Run 'bash setup.sh' first."
    exit 1
fi

rm -rf "$STAGE" "$OUTDIR/$BUNDLE_NAME.run"
mkdir -p "$STAGE"

echo "[1/4] Copying voicebot binary..."
cp build/voicebot "$STAGE/"

echo "[2/4] Copying Piper runtime..."
mkdir -p "$STAGE/piper"
cp piper/piper "$STAGE/piper/"
cp piper/libespeak-ng.so.1.52.0.1 "$STAGE/piper/"
ln -sf libespeak-ng.so.1.52.0.1 "$STAGE/piper/libespeak-ng.so.1"
ln -sf libespeak-ng.so.1 "$STAGE/piper/libespeak-ng.so"
cp piper/libonnxruntime.so.1.14.1 "$STAGE/piper/"
ln -sf libonnxruntime.so.1.14.1 "$STAGE/piper/libonnxruntime.so"
cp piper/libpiper_phonemize.so.1.2.0 "$STAGE/piper/"
ln -sf libpiper_phonemize.so.1.2.0 "$STAGE/piper/libpiper_phonemize.so.1"
ln -sf libpiper_phonemize.so.1 "$STAGE/piper/libpiper_phonemize.so"
cp piper/libtashkeel_model.ort "$STAGE/piper/"
cp -r piper/espeak-ng-data "$STAGE/piper/"

echo "[3/4] Copying voice model..."
mkdir -p "$STAGE/models"
cp models/*.onnx "$STAGE/models/"
cp models/*.onnx.json "$STAGE/models/"

echo "[4/4] Creating self-extracting archive..."

# Create the compressed tarball
TARBALL="$OUTDIR/payload.tar.gz"
tar czf "$TARBALL" -C "$STAGE" .

# Create the self-extracting runner script
cat > "$OUTDIR/$BUNDLE_NAME.run" << 'RUNNER_EOF'
#!/bin/bash
set -e

EXTRACT_DIR="${VOICEBOT_DIR:-$(mktemp -d /tmp/voicebot.XXXXXX)}"
ARCHIVE_LINE=$(awk '/^__ARCHIVE_BELOW__$/{print NR + 1; exit 0}' "$0")

echo "Extracting to $EXTRACT_DIR..."
tail -n +"$ARCHIVE_LINE" "$0" | tar xz -C "$EXTRACT_DIR"

if [ ! -f "$EXTRACT_DIR/.env" ] && [ -n "$OPENAI_API_KEY" ]; then
    echo "OPENAI_API_KEY=$OPENAI_API_KEY" > "$EXTRACT_DIR/.env"
fi

if [ ! -f "$EXTRACT_DIR/.env" ]; then
    echo ""
    echo "WARNING: No .env file and OPENAI_API_KEY not set."
    echo "Run with: OPENAI_API_KEY=sk-... $0"
    echo "Or create $EXTRACT_DIR/.env with OPENAI_API_KEY=sk-..."
    echo ""
fi

cd "$EXTRACT_DIR"
exec ./voicebot "$@"

__ARCHIVE_BELOW__
RUNNER_EOF

# Append the tarball to the runner script
cat "$TARBALL" >> "$OUTDIR/$BUNDLE_NAME.run"
chmod +x "$OUTDIR/$BUNDLE_NAME.run"
rm -f "$TARBALL"
rm -rf "$STAGE"

SIZE=$(du -h "$OUTDIR/$BUNDLE_NAME.run" | cut -f1)
echo ""
echo "=== Done ==="
echo "Output: $OUTDIR/$BUNDLE_NAME.run ($SIZE)"
echo ""
echo "Usage:"
echo "  OPENAI_API_KEY=sk-... ./$OUTDIR/$BUNDLE_NAME.run"
echo ""
echo "Or set VOICEBOT_DIR to extract to a fixed location:"
echo "  VOICEBOT_DIR=~/voicebot OPENAI_API_KEY=sk-... ./$OUTDIR/$BUNDLE_NAME.run"
