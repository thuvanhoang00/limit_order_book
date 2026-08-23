# GitHub pull request reviews

## Reviewer identity

- Repository owner/default account: `thuvanhoang00`.
- Dedicated reviewer account: `f33voz2-debug`.
- Reviewer GitHub CLI profile: `/home/thu/.config/gh-reviewer`.
- If reauthentication is required, run:
  `BROWSER=true GH_CONFIG_DIR=/home/thu/.config/gh-reviewer gh auth login --hostname github.com --web`
  and complete the device flow in a private browser window.

## Rules

- For every GitHub pull-request review operation, invoke GitHub CLI with the dedicated reviewer profile:
  `GH_CONFIG_DIR=/home/thu/.config/gh-reviewer gh ...`
- Before posting a review, comment, approval, or change request, run
  `GH_CONFIG_DIR=/home/thu/.config/gh-reviewer gh auth status` and verify that the active account is exactly `f33voz2-debug`.
- If the reviewer profile is missing, unauthenticated, or resolves to any account other than `f33voz2-debug`, do not submit anything to GitHub. Tell the user that the reviewer login must be completed first.
- Never use the default GitHub CLI profile to submit PR-review activity for this repository.
- Reviewing locally is read-only. Only post comments, approvals, or change requests to GitHub when the user explicitly asks to submit them.
- Never print, commit, or store GitHub tokens or credentials in this repository.
