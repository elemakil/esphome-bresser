# Contributing

Issues and pull requests are welcome. CI runs the checks below on every pull request; please
run them locally first.

## Layout

```
components/bresser/
  bresser_decoder.{h,cpp}   frame decoder, plain C++17 without ESPHome
  bresser.{h,cpp}           the ESPHome component: radio listener, devices, entities
  __init__.py, sensor.py, binary_sensor.py, text_sensor.py   configuration and codegen
examples/                   complete device configurations
tests/                      decoder test against rtl_433 (see tests/README.md)
tools/                      example builds, config validation tests
docs/                       frame format, radio settings, board notes
```

## Tests

```sh
python3 tests/run_golden.py         # needs g++
tools/config_test/run.sh            # needs ESPHome, see below
tools/build_examples.sh config      # validate both examples
tools/build_examples.sh compile     # build both examples (slow the first time)
```

- `tests/run_golden.py` builds the decoder for the host and compares it with rtl_433's output
  for 1556 payloads. Any change to the decoder must keep all of them passing.
- `tools/config_test/run.sh` validates the fixtures in `tools/config_test/fixtures/`:
  `good_*.yaml` must be accepted, `bad_*.yaml` rejected with the message given on their first
  line (`# expect: ...`).
- `tools/build_examples.sh` builds `examples/*.yaml` against this checkout instead of GitHub,
  with throwaway secrets. Only a compile type-checks the C++ and lambdas.

## Running ESPHome

Running the config tests and building the examples needs ESPHome on your `PATH`: install the
pinned release with `pip install esphome==2026.10.1`, or run the commands inside the
container `ghcr.io/esphome/esphome:2026.10.1`, as CI does. The scripts exit with a message
if `esphome` is missing.

## Style

- C++: `clang-format` with the repository's `.clang-format` (ESPHome's own).
- Python: `ruff check` and `ruff format`.
- Shell: `shellcheck` and `shfmt -i 4 -ci`.
- YAML: `yamllint`.

The exact commands and versions are in `.github/workflows/ci.yml`.
