SELECT e.ino, e.path FROM entry e JOIN attr a0 ON a0.eid = e.id AND a0.name = 'namespace' AND a0.type = 1 AND a0.v = x'6e7333' JOIN attr a1 ON a1.eid = e.id AND a1.name = 'agent' AND a1.type = 1 AND a1.v = x'613037' WHERE e.ok = 1 ORDER BY e.ino LIMIT -1
SELECT e.ino, e.path FROM entry e JOIN attr a0 ON a0.eid = e.id AND a0.name = 'agent' AND a0.type = 1 AND a0.v = x'613037' WHERE e.ok = 1 ORDER BY e.ino LIMIT -1
SELECT e.ino, e.path FROM entry e JOIN attr a0 ON a0.eid = e.id AND a0.name = 'kind' AND a0.type = 1 AND a0.v = x'6e6f7465' WHERE e.ok = 1 ORDER BY e.ino LIMIT -1
