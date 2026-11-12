import unittest

from server_mounts import batch_sql


class ServerMountTests(unittest.TestCase):
    def test_batch_keeps_all_mounts_in_one_guarded_transaction(self):
        manifests = [dict(source_key=f'hero:test:{i}', definition_sha256='a' * 64,
                          target=dict(kind='mount', item_id=i, display_id=i,
                                      records={'item_template': {'entry': i, 'name': "Butcher's Beast"}})) for i in (1, 2)]
        sql = batch_sql(manifests)
        self.assertEqual(sql.count('START TRANSACTION;'), 1)
        self.assertEqual(sql.count('\nCOMMIT;'), 1)
        self.assertEqual(sql.count('CREATE TEMPORARY TABLE'), 1)
        self.assertEqual(sql.count('INSERT INTO `item_template`'), 2)
        self.assertEqual(sql.count('SELECT GET_LOCK('), 1)
        self.assertNotIn('DELETE FROM', sql)
        self.assertNotIn("Butcher's Beast", sql)
        self.assertIn('CHECK (`ok` = 1)', sql)


if __name__ == '__main__':
    unittest.main()
