# Reporting a problem

Report ordinary bugs through Issues. For a suspected vulnerability that exposes user data,
avoid publishing tokens, dumps or console contents. Use GitHub's private vulnerability report
feature if available, or contact the maintainer through the public profile to arrange a
private channel. Do not place secret material in a public issue.

The store downloads executable game packages. It uses HTTPS verification, bounded parsing,
staged writes and available integrity metadata, but cannot guarantee third-party packages.
Keep dependency notices and validation limitations visible. Debug builds can dump private
memory; the published build disables memory dumps and has no upload endpoint.
