Run from the project root:

```bash
bash member_d/scripts/build_team.sh
ctest --test-dir build --output-on-failure -R 'measurement|allocation|runner_python'
```
