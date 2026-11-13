需要 Apple Silicon Mac、Python 3 和 Homebrew。在项目目录运行：

```bash
brew install cmake libomp
bash member_d/scripts/build_team.sh
python3 member_d/scripts/demo.py --out results/demo
```

结果保存在 `results/demo/results/summary.csv`。再次运行时换一个输出目录，例如 `results/demo2`。
