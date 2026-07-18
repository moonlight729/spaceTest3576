# Fingerprint hardware interface

Current implementation is an explicit temporary bypass: `fingerprint_run_test()` returns PASS without accessing hardware. The public C API is final for the current phase; when the SPI module is available, replace only the internals with open/reset/status/self-test logic and retain the same API.
