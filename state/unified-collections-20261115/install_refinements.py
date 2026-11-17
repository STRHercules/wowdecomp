from datetime import datetime
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tools/CosmeticsPack'))
from package_unified_collections import package,install_live
client=Path('G:/3.3.5a - Dev')
out=ROOT/'state'/('unified-collections-live-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
receipt=package(client,out)
install_live(receipt,client)
