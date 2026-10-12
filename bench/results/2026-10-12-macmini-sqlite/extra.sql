-- statements 10..14: the QUERY2 shapes of journal 0044 in SQL (the domain is the namespace column)
SELECT ino, path FROM memories WHERE namespace = 'ns3' AND agent = 'a07' AND kind = 'note' ORDER BY ino;
SELECT ino, path FROM memories WHERE namespace = 'ns3' AND agent = 'a07' ORDER BY ino;
SELECT ino, path FROM memories WHERE namespace = 'ns3' AND kind = 'note' ORDER BY created DESC LIMIT 20;
SELECT ino, path FROM memories WHERE namespace = 'ns3' AND agent = 'a07' ORDER BY created DESC LIMIT 20;
