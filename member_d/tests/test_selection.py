import importlib.util
import unittest
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'scripts/freeze_parameters.py'
spec=importlib.util.spec_from_file_location('selection',p)
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

class SelectionTests(unittest.TestCase):
    def test_selects_joint_geometric_score(self):
        pairs=[(16,128,9,1),(16,512,1,4),(32,128,2,3),(32,512,4,4),(64,128,3,3),(64,512,9,9)]
        rows=[dict(tile_features=tf,max_chunk_edges=te,family=family,median_seconds=t) for tf,te,u,s in pairs for family,t in [('uniform',u),('skewed',s)]]
        winner=module.select(rows)[0]
        self.assertEqual((winner['tile_features'],winner['max_chunk_edges']),(16,512))
        self.assertEqual(winner['geometric_mean_seconds'],2)

    def test_missing_family_is_rejected(self):
        with self.assertRaises(ValueError):module.select([dict(tile_features=16,max_chunk_edges=128,family='uniform',median_seconds=1)])

if __name__=='__main__':unittest.main()
