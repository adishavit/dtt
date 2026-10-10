"""Create an allowlisted Railway upload directory; never upload the repo root."""
from pathlib import Path
import shutil
import tempfile

source = Path(__file__).resolve().parent
target = Path(tempfile.mkdtemp(prefix='dtt-web-demo-'))
for name in ('Dockerfile', 'package.json', 'package-lock.json', 'server.mjs', 'http.mjs', 'warm.mjs'):
    shutil.copy2(source / name, target / name)
shutil.copy2(source.parent / 'index.html', target / 'index.html')
print(target)
