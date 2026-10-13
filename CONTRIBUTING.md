# Contributing to geany-lopp

Thanks for considering a contribution. Bug reports, docs fixes, and small
improvements are all welcome.

## Dev setup

```bash
git clone https://github.com/Avicennasis/geany-lopp.git
cd geany-lopp
sudo apt install libgtk-3-dev geany-common   # build deps
make
```

## Running the tests

```bash
make test
```

## PR checklist

- [ ] Tests added or updated; local test run is green.
- [ ] README and docs updated if public behavior changed.
- [ ] `CHANGELOG.md` updated under `[Unreleased]`.

## Code of Conduct

This project follows the [Contributor Covenant](CODE_OF_CONDUCT.md).
Be respectful; assume good faith.
