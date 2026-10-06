# Contributing to DXF Sketcher

Thank you for helping improve a practical workshop tool. Bug reports, small reproducible drawings, documentation corrections and focused patches are welcome.

## Before changing behavior

Search the [existing issues](https://github.com/EriArk/-DXF-Sketcher/issues) first. For a substantial feature or refactor, describe the problem and proposed scope in an issue before starting. An open issue is planned work, not a claim that implementation has begun.

Include your OS, application version, exact steps, expected behavior and actual result. Attach the smallest DXF that reproduces the problem only if you can share it publicly. Remove private drawing content and paths from logs and screenshots.

## Local development

Follow [BUILDING.md](BUILDING.md). [DEVELOPMENT.md](DEVELOPMENT.md) maps the source and current verification approach.

GitHub Actions is disabled by maintainer decision as of 6 October 2026. Validation runs locally; no automated GitHub check is promised. Existing workflow definitions, where present, are retained as historical build recipes. Do not enable Actions or add hosted automation as part of an unrelated patch.

## Preparing a pull request

- Keep one concrete problem per PR and link its issue.
- Explain the previous behavior and what changes for a user.
- List the commands and manual workflows actually checked, including OS and edition. State checks you could not run.
- Include before/after screenshots for visible UI changes and small fixtures for DXF compatibility changes.
- Preserve existing files, settings and normal workflows. Document import/export limitations rather than hiding them.
- Keep third-party copyright and license notices; do not mix unrelated dependency upgrades or mass formatting into a behavior fix.
- Run `git diff --check` before submitting.

Use clear, respectful discussion. For security concerns, follow [SECURITY.md](SECURITY.md) instead of posting private details in a public issue.
