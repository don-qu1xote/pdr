"""Контур первого процесса ПДР.

Оснастка общая с остальными наборами, которые поднимают этот же процесс, и лежит
в tests/contour.py: два представления об одной базе разошлись бы молча, оставив
оба набора зелёными.
"""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tests'))

pytest_plugins = ['pytest_userver.plugins.postgresql', 'contour']
