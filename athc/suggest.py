"""Edit-distance "did you mean?" suggestions for diagnostics.

A single Damerau-Levenshtein implementation (adjacent transposition counts
as one edit, so `improt`->`import` is distance 1) plus `closest`, which
picks the nearest candidate within a threshold."""


def edit_distance(a: str, b: str) -> int:
    """Damerau-Levenshtein distance (optimal string alignment variant)."""
    la, lb = len(a), len(b)
    if la == 0:
        return lb
    if lb == 0:
        return la
    # d[i][j] = distance between a[:i] and b[:j].
    prev2 = None
    prev = list(range(lb + 1))
    for i in range(1, la + 1):
        cur = [i] + [0] * lb
        for j in range(1, lb + 1):
            cost = 0 if a[i - 1] == b[j - 1] else 1
            cur[j] = min(
                prev[j] + 1,        # deletion
                cur[j - 1] + 1,     # insertion
                prev[j - 1] + cost,  # substitution
            )
            if (
                i > 1
                and j > 1
                and a[i - 1] == b[j - 2]
                and a[i - 2] == b[j - 1]
            ):
                cur[j] = min(cur[j], prev2[j - 2] + 1)  # transposition
        prev2, prev = prev, cur
    return prev[lb]


def closest(
    name: str,
    candidates,
    *,
    fold: bool = False,
    max_distance: int = 2,
) -> str | None:
    """Return the single nearest candidate to `name` within `max_distance`,
    or None. With `fold`, comparison is case-insensitive (for keywords and
    function names). Short names get a tighter bound so they don't
    over-suggest. Ties resolve to the first candidate in sorted order."""
    key = name.lower() if fold else name
    # Cap the bound for short names so 1-char typos don't match everything
    # (a 1-char name gets bound 0 → no suggestion).
    bound = min(max_distance, len(name) // 2)
    best = None
    best_d = bound + 1
    for cand in sorted(candidates):
        ckey = cand.lower() if fold else cand
        if ckey == key:
            continue  # identical (modulo fold) — not a useful suggestion
        d = edit_distance(key, ckey)
        if d < best_d:
            best_d, best = d, cand
    return best if best_d <= bound else None
