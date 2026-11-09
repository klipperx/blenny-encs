# Host regression suite

These pytest tests mirror firmware contracts in Python so we can catch
regressions without a badge. When you change behavior in `src/*.cpp` /
`src/*.h`, update the matching mirror under `tests/blenny/` in the same change.

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r tools/requirements.txt -r tools/requirements-dev.txt
pytest -q
```

No live ESPN/MLB network calls. Fixtures under `tests/fixtures/` are synthetic
JSON shaped like the fields the firmware already reads.
