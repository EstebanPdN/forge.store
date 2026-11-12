# Contributing

Keep the approved low-resolution interface, title identity, user files and save compatibility.
Make changes in the maintained web modules or `3ds/source/`; do not modify `versions/v1/`.
Use shared tabs/buttons and existing drawing helpers before inventing another component.
Describe the observable behavior and run relevant checks from [Building](docs/BUILDING.md).

For native work, state separately whether you compiled, tested on a host, ran in an emulator
or tested on an identified physical console. Record Old/New model, CIA/3DSX and build hash.
Add tests for behavior that can lose data, race, corrupt media or break installation.

Document module and thread ownership in headers. Comment decisions and invariants rather
than restating every line. Keep runtime dependencies and generated assets reproducible.
Do not reformat the whole project alongside an unrelated fix.

Preserve third-party licenses. Record asset sources and processing in the asset register;
unknown permission stays unknown. Never commit ROMs, firmware, saves, private dumps,
credentials, local caches or private repository content.

Open a pull request with what changed, why and the checks actually run. No release or
website deployment should be triggered by a documentation or source publication.
