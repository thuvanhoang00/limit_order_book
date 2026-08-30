# GitHub pull request reviews

## Reviewer identity

- Repository owner/default account: `thuvanhoang00`.
- Dedicated reviewer account: `f33voz2-debug`.
- Reviewer GitHub CLI profile: `/home/thu/.config/gh-reviewer`.
- If reauthentication is required, run:
  `BROWSER=true GH_CONFIG_DIR=/home/thu/.config/gh-reviewer gh auth login --hostname github.com --web`
  and complete the device flow in a private browser window.

## Rules

- Pull-request merges are always the repository owner's responsibility. Never run
  `gh pr merge` or call a GitHub API that merges a pull request; stop after submitting
  the review and report its outcome to the user.
- For every GitHub pull-request review operation, invoke GitHub CLI with the dedicated reviewer profile:
  `GH_CONFIG_DIR=/home/thu/.config/gh-reviewer gh ...`
- Before posting a review, comment, approval, or change request, run
  `GH_CONFIG_DIR=/home/thu/.config/gh-reviewer gh auth status` and verify that the active account is exactly `f33voz2-debug`.
- If the reviewer profile is missing, unauthenticated, or resolves to any account other than `f33voz2-debug`, do not submit anything to GitHub. Tell the user that the reviewer login must be completed first.
- Never use the default GitHub CLI profile to submit PR-review activity for this repository.
- A user request to review a GitHub PR authorizes both the read-only analysis and submission of the resulting review to GitHub. Do not wait for a separate `submit` instruction.
- Submit `REQUEST_CHANGES` when there are blocking findings, `APPROVE` when there are no findings, and `COMMENT` when all findings are non-blocking or an approval is inappropriate.
- Post each finding as an inline review comment when its relevant line is part of the PR diff. Use the overall review body only for the summary, verification evidence, or findings that cannot be attached to a changed line.
- Do not duplicate the full text of an inline finding in the overall review body; summarize it there and let the inline comment carry the file-specific detail.
- Before submitting, verify that every inline comment targets the latest PR head commit and a valid changed line on the right side of the diff.
- Keep the review local and do not submit only when the user explicitly asks for a draft, local-only review, or no GitHub post.
- Never print, commit, or store GitHub tokens or credentials in this repository.
