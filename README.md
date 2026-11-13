Requires an Apple Silicon Mac, Python 3, and Homebrew. Run from the project directory:

```bash
brew install cmake libomp
bash member_d/scripts/build_team.sh
python3 member_d/scripts/demo.py --out results/demo
```

Results are saved to `results/demo/results/summary.csv`. Use a new output directory for each run, such as `results/demo2`.
