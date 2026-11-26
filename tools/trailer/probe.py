"""Quick probes of the app (shots only, no recording): python probe.py <name> ..."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tb import *  # noqa


def launcher_times():
    s = S(30)
    for f in (60, 600, 1200, 1800, 2400, 3000):
        s.at(f).cmd('shot probe_l_%d' % f)
    s.at(3010)
    return launch('probe_launcher', settings_ini('md'), s.text())


def nav():
    s = S(30).wait(200).cmd('shot pn_0').tap('down', gap=30).cmd('shot pn_1').tap('enter', gap=60).cmd('shot pn_2')
    s.tap('esc', gap=40).tap('left', gap=30).cmd('shot pn_3').tap('enter', gap=60).cmd('shot pn_4')
    s.tap('down', 3, gap=20).tap('enter', gap=60).cmd('shot pn_5').tap('esc', gap=30).tap('esc', gap=40)
    s.tap('up', gap=30).tap('enter', gap=200).cmd('shot pn_6').tap('esc', gap=60).cmd('shot pn_7')
    return launch('probe_nav', settings_ini('md', launcher={}, extra={'updates': {'check': 1, 'latest': 'v1.1'}}),
                  s.text())


PROBES = {'launcher': launcher_times, 'nav': nav}

if __name__ == '__main__':
    run_all([(n, PROBES[n]) for n in sys.argv[1:]])
