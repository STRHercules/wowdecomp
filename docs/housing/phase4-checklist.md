# Phase 4 in-game checklist: claiming a Founder's Point plot

Stage realm `esteria-housing:slice4`, same setup as [phase1-checklist.md](phase1-checklist.md). Use an account with no
plot. A house claimed at the steward is fine; it moves onto the plot you claim.

| # | Do | Expect |
|---|---|---|
| 1 | Steward → **Visit Founder's Point**, then walk to a plot. Plot 1's sign is near (2942, −214). | A "for sale" sign stands at each plot. |
| 2 | Click a sign. | Gossip: **Claim this plot (N).** If nothing opens, report it; the sign's GameObject type then needs changing. |
| 3 | Claim it. | "The plot is yours." A small Human House appears with base, roof, windows and a chimney, and its door faces the sign. Note if the house faces away or has holes in its walls. |
| 4 | Walk into the front door. | Loading screen, then your Home Interior rooms and decor. |
| 5 | Steward inside → **Leave the house**. | You arrive at the plot, beside the sign. |
| 6 | Click the sign again. | **Enter your house.** works too. |
| 7 | Second account, not grouped: click your sign, then walk into the door. | "This plot is someone's home…", and the door does nothing. Grouped with you: **Visit this house.**, and the door works. |
| 8 | Try to claim a second plot with the same account. | "Your house already stands on another plot." |
| 9 | Restart the stage (`docker restart housing-stage`). | The house is still on the plot. |

## Send back

- Screenshots of the house from the front (with the sign) and from the side.
- `.gps` at the door if walking in doesn't work.
- Stage log lines `housing: … claimed plot …`.

## Undo

- `DELETE FROM housing_plot_claim;` in `housing_dev_characters`.
- `DELETE FROM gameobject WHERE map = 2735 AND id BETWEEN 9500150 AND 9500198;` in `housing_dev_world` removes saved
  exteriors.
- Restart the stage. Older stage images (`slice3c`) still work against these tables.
