#!/usr/bin/env python3
"""Exercise the common popup through Desktop, Atlas and the ASTRA menu."""
import argparse
import importlib.util
from pathlib import Path
import re
import shutil
import tempfile
import time

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('desktop_apps', HERE / 'test-desktop-apps.py')
desktop = importlib.util.module_from_spec(spec)
spec.loader.exec_module(desktop)
terminal = desktop.terminal_gate
spec = importlib.util.spec_from_file_location('atlas_gate', HERE / 'test-atlas.py')
atlas = importlib.util.module_from_spec(spec)
spec.loader.exec_module(atlas)


def click(machine, x, y, button='left'):
    machine.qmp.point(x, y)
    machine.qmp.input_events([{'type': 'btn', 'data': {'button': button, 'down': down}}
                             for down in (True, False)])


def open_popup(machine, point):
    desktop.wait_quiet(machine, 'before popup', seconds=.2)
    before = desktop.display_count(machine, 'render-batches')
    click(machine, *point, button='right')
    desktop.wait_quiet(machine, 'context menu', seconds=.2)
    assert desktop.display_count(machine, 'render-batches') > before, 'right click showed no popup'


def new_lines(machine, before):
    return machine.syslog_lines()[before:]


def wait_atlas(machine, before, deadline):
    end = time.monotonic() + deadline
    while time.monotonic() < end:
        lines = new_lines(machine, before)
        roots = [line for line in lines if 'atlas root /dh0 entries ' in line]
        if roots:
            assert re.search(r'entries [1-9][0-9]* folders [0-9]+ status 0$', roots[-1]), roots
            desktop.wait_quiet(machine, 'Atlas context launch', seconds=.2)
            return
        assert not any('desktop menu command' in line or 'desktop context menu' in line or
                       'application launch refused' in line for line in lines), lines[-20:]
        time.sleep(.05)
    raise RuntimeError('context Open did not enumerate DH0: %s' % new_lines(machine, before)[-25:])


def no_launch(machine, before):
    assert not any('launch /apps/Atlas.app' in line for line in new_lines(machine, before)), \
        'dismissal or pointer down launched an application'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('qemu')
    parser.add_argument('rom')
    parser.add_argument('image')
    parser.add_argument('--deadline', type=float, default=60)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='astra-context-menu-') as work:
        image = str(Path(work) / 'system.img')
        shutil.copyfile(args.image, image)
        machine = terminal.Machine(args.qemu, args.rom, image, work)
        try:
            assert terminal.open_terminal(machine, 120, args.deadline), 'boot did not finish'
            point = machine.desktop_icon('/dh0')
            before = len(machine.syslog_lines())
            open_popup(machine, point)
            machine.qmp.key('esc')
            desktop.wait_quiet(machine, 'Escape dismissal', seconds=.2)
            no_launch(machine, before)
            open_popup(machine, point)
            # Clicking another icon with the secondary button dismisses this
            # popup; its release must not open a new popup under the old one.
            other = machine.desktop_icon('/apps/Terminal.app')
            machine.qmp.point(*other)
            desktop.wait_quiet(machine, 'outside menu motion', seconds=.2)
            batches = desktop.display_count(machine, 'render-batches')
            click(machine, *other, button='right')
            desktop.wait_quiet(machine, 'outside dismissal', seconds=.2)
            assert desktop.display_count(machine, 'render-batches') == batches + 1, \
                'outside release fell through to another context menu'
            no_launch(machine, before)
            open_popup(machine, point)
            machine.qmp.point(point[0] + 24, point[1] + 24)
            desktop.wait_quiet(machine, 'Open hover', seconds=.2)
            machine.qmp.button(True)
            time.sleep(.2)
            no_launch(machine, before)
            machine.qmp.button(False)
            wait_atlas(machine, before, args.deadline)
            click(machine, 360 + 760 - desktop.CLOSE_INSET, 120 + desktop.TITLE_Y)
            desktop.wait_quiet(machine, 'Atlas close', seconds=.2)
            before = len(machine.syslog_lines())
            open_popup(machine, point)
            machine.qmp.key('down')
            machine.qmp.key('ret')
            wait_atlas(machine, before, args.deadline)
            click(machine, 360 + 760 - desktop.CLOSE_INSET, 120 + desktop.TITLE_Y)
            desktop.wait_quiet(machine, 'Atlas keyboard close', seconds=.2)
            # A menu's terminating Enter must not execute this unfinished shell line.
            machine.qmp.type_text('print MENU-ENTER-GUARD')
            guard_sequence = machine.sequence()
            # Same menu control owns keyboard interaction in the ASTRA dropdown.
            click(machine, 40, 15)
            machine.qmp.key('end')
            machine.qmp.key('esc')
            desktop.wait_quiet(machine, 'ASTRA keyboard dismissal', seconds=.2)
            click(machine, 40, 15)
            machine.qmp.key('down')
            desktop.wait_quiet(machine, 'About menu selection', seconds=.2)
            before_about = desktop.display_count(machine, 'render-batches')
            machine.qmp.key('ret')
            desktop.wait_quiet(machine, 'About from shared keyboard menu', seconds=.2)
            assert desktop.display_count(machine, 'render-batches') >= before_about + 2, \
                'shared ASTRA keyboard selection opened no About dialog'
            click(machine, 1158, 481)
            desktop.wait_quiet(machine, 'About close', seconds=.2)
            assert terminal.wait_for_command(machine, 'MENU-ENTER-GUARD', .3,
                guard_sequence, exact=True) is None, 'menu Enter reached the underlying shell'
            machine.qmp.key('ret')
            assert terminal.wait_for_command(machine, 'MENU-ENTER-GUARD', args.deadline,
                guard_sequence, exact=True) is not None, 'normal Enter did not recover after menu'

            desktop.require_pointer(machine, ((200, 200), (1500, 700)))
            assert machine.process.poll() is None, 'QEMU exited'
            assert not any('PANIC' in line for line in machine.log), machine.recent_serial()
            atlas.check_retired(machine, args.deadline)
            print('ASTRA CONTEXT MENUS PASS right click, Escape/outside dismissal, release commit, '
                  'DH0 Open by pointer/keyboard, shared ASTRA keyboard menu', flush=True)
        finally:
            machine.close()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
