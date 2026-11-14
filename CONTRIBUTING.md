[//]: # (Do not modify this file manually.)
[//]: # (This file is generated from the docs directory by executing `make clean; make`.)

# Contributing

## Committing

When committing, use semantic commit messages. If a commit include specific hardware code, a real verifiable email address is required.

## Pull requests

Contributions are always welcome but, sometimes, they might be in conflict with long term project goals. This is particulary true for pull requests affecting several files and internal project structures, and for specific features that try to circumvent them.

In order to prevent these type of conflicts, create an issue beforehand to discuss these improvements and how to properly implement them.

## Code formatting

Elektroid uses the default indenting style provided by `indent`, which is the GNU style. It should be applied when submitting contributions by running `make format`.

## Unit tests

Running `make check` will run some unit tests. The more unit tests, the better.

## Integration tests

After running `sudo make install`, running `make installcheck` will run some integration tests. Some of these are run over the `system` filesystem but others are run over specific filesystems that require the actual devices to be connected.

Ideally, new integration tests should be implemented when new devices are added.

In order to run one of these tests, proceed as follows. The variable `TEST_DEVICE` must contain the device id and variable `TEST_CONNECTOR_FILESYSTEM` must contain the connector name, an underscore char (`_`) and the filesystem name.

```
$ TEST_DEVICE=1 TEST_CONNECTOR_FILESYSTEM=efactor_preset make installcheck
```

## Documentation

`README.md` and other resource file are generated from the `docs` dir, which contains the web page of the project in Jekyll format. In order to regenerate the web page and these files, run `make clean; make` from the `docs` directory.
