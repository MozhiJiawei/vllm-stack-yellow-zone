echo "[DEBUG] Start building..."

echo "$1"

if [ "$1" = "dt" ]; then
    echo "[DEBUG] Running DT..."
else
    echo "[DEBUG] Running build..."
fi