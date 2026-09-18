"""Replay measured inputs through the production force state machine.

These assertions verify waveform decisions, not physical or OS double-clicks.
The fixture keeps every captured sensor frame, including unlabelled intervals.
"""
import json
from pathlib import Path
import subprocess
import sys

runner = sys.argv[1]
total_pairs = 0
results = []
fixture_dir = Path(sys.argv[2])
for path in sorted(fixture_dir.glob('force-double-v20-g*.json')):
    doc = json.loads(path.read_text())
    payload = ''.join(' '.join(map(str, row)) + '\n' for row in doc['samples'])
    run = subprocess.run([runner, *map(str, doc['levels'])], input=payload,
                         text=True, capture_output=True, check=True)
    events = [tuple(map(int, line.split(','))) for line in run.stdout.splitlines() if ',' in line]
    assert all(event == 2 for _, event, _ in events), (path, 'force waveform became a drag')
    assert 'held_at_end=0' in run.stdout
    windows = doc['pair_windows_ms']
    counts = []
    gaps = []
    for (start, end), expected in zip(windows, doc['expected_pair_counts'], strict=True):
        clicks = [t for t, _, _ in events if start <= t <= end]
        assert len(clicks) == expected, (path.name, start, end, clicks, expected)
        if expected == 2:
            assert 0 < clicks[1] - clicks[0] <= 500, (path.name, clicks)
            total_pairs += 1
            gaps.append(clicks[1] - clicks[0])
        counts.append(len(clicks))
    outside = [t for t, _, _ in events if not any(a <= t <= b for a,b in windows)]
    results.append(dict(group=doc['group'], counts=counts, gaps_ms=gaps,
                        unlabelled_click_times_ms=outside))
assert len(results) == 4 and total_pairs == 19, results
print(json.dumps(dict(replayed_two_click_waveforms=total_pairs, total_windows=20,
    note='One first peak below absolute click floor; unlabelled events are not proof of false or intended clicks. No hardware/OS success claim.', groups=results)))
