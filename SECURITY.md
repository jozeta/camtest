# Security Policy

## Reporting a vulnerability

Please **do not open a public issue** for security problems.

Report it privately through GitHub instead: go to the **Security** tab of this repository and click
**Report a vulnerability** ([direct link](https://github.com/jozeta/camtest/security/advisories/new)).
Include what you found, how to reproduce it, and the impact you expect.

This is a personal project maintained in spare time. You can expect an acknowledgement within a
week. Fixes are released as soon as practical, and you'll be credited in the advisory unless you
prefer otherwise.

## Scope

Of particular interest: crashes or memory-safety issues in `camtest_filter.dll`, which is loaded into other applications' processes, and abuse of the named-pipe protocol.

## Supported versions

Only the latest version on the `main` branch receives security fixes.
