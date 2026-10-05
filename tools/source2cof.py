"""Convert a Source engine map into a Cry of Fear map. See source2cof/convert.py.

    py tools/source2cof.py "<Source game>/<game>/maps/<map>.bsp" [--name NAME]
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from source2cof.convert import main

if __name__ == "__main__":
    sys.exit(main())
