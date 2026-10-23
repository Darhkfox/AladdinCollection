"""Achievement checks on real game flow (tools/smst.py Ach feeds every frame's RAM to achievement_frame).

    python sms_ach_test.py

From power-on: GAME START, the story scenes skipped with both buttons, each stage left through the game's own
exit routine ($35B8: stage + 1, its story scene) after some play, Jafar's hit counter set to 8 so the stage hands
over to the ending. Prints every event with the frame; a second run takes a hit in stage 0 and goes through a game
over; a third enters stage 3 by password.
"""
import sms_re as R
from smst import Ach

ACH = ['market', 'rooftops', 'cave', 'treasure', 'lamp', 'escape', 'prince', 'carpet', 'palace', 'jafar', 'newgame',
       'nocontinue', 'noscratch', 'flawless', 'jafar_flawless', 'stones', 'skip']


class Run:
    def __init__(self):
        self.s = R.boot()
        self.a = Ach('sms')
        self.events = []

    def frame(self, buttons=0, n=1):
        for _ in range(n):
            self.s.run(1, buttons)
            for kind, value, index in self.a.step(self.s.ram):
                self.events.append((self.s.frame, ['unlock', 'count', 'mark', 'progress'][kind], ACH[index], value))

    def until(self, cond, limit, buttons=0):
        for _ in range(limit):
            if cond():
                return True
            self.frame(buttons)
        return cond()


def to_game_start(r, password=None):
    s = r.s
    r.until(lambda: s.ram8(0xC200) == 0 and s.ram8(0xCF01) == 0x12, 3000, 0)
    if password:
        for t in range(10):
            if s.ram8(0xCF24) & 1:
                break
            r.frame('DOWN', 3); r.frame(0, 8)
    for t in range(30):
        r.frame('1', 2)
        if r.until(lambda: s.ram8(0xC200) != 0, 10):
            break
    r.until(lambda: s.ram8(0xC200) not in (0, 0x0F) or s.ram8(0xC105) != 0 or False, 1)
    if password:
        r.until(lambda: False, 180)
        for t in range(40):
            for i, ch in enumerate(password):
                s.set8(0xC924 + i, ord(ch) - 65)
            r.frame('1', 2); r.frame(0, 4)
            if 2 <= s.ram8(0xC200) <= 0x0B:
                break


def skip_scenes(r):
    s = r.s
    while s.ram8(0xC200) == 0x13:
        r.frame('1+2', 2)
        r.frame(0, 6)


def play_stage(r, frames=200, hit=False):
    s = r.s
    r.until(lambda: 2 <= s.ram8(0xC200) <= 0x0B and s.ram8(0xC912) == 0, 600)
    r.frame(0, frames)
    if hit:
        s.set8(0xC116, s.ram8(0xC116) - 1)
        r.frame(0, 2)
    print('  played stage %d (mode %02X) to frame %d' % (s.ram8(0xC105), s.ram8(0xC200), s.frame))


def leave_stage(r):
    s = r.s
    if s.ram8(0xC105) == 9:
        s.set8(0xC6AD, 8)   # (as the eighth hit leaves it: $27DE)
        s.set8(0xC6AB, 1)
        s.set8(0xC6AC, 1)
        r.until(lambda: s.ram8(0xC200) == 0x13, 600)
    else:
        s.set_cpu('pc', 0x35B8)
        r.until(lambda: s.ram8(0xC200) != s.ram8(0xC105) + 1, 600)
    r.frame(0, 60)


if __name__ == '__main__':
    r = Run()
    to_game_start(r)
    for st in range(10):
        skip_scenes(r)
        play_stage(r, hit=(st == 4))
        print('stage', r.s.ram8(0xC105), 'mode %02X' % r.s.ram8(0xC200))
        leave_stage(r)
    print('run 1 (GAME START, a hit in stage 4):')
    for e in r.events:
        print('  ', e)
    # game over then continue
    r = Run()
    to_game_start(r)
    skip_scenes(r)
    play_stage(r)
    r.s.set8(0xC116, 1)
    r.s.set8(0xC116, 0)
    r.s.set_cpu('pc', 0x1C26)   # the game's own death (fall) routine
    r.until(lambda: r.s.ram8(0xC200) == 0x11, 600)
    r.frame(0, 60)
    r.frame('1', 2)
    play_stage(r)
    print('run 2 (game over, continue): run bits %d' % r.a.run_bits, r.events)
    r = Run()
    to_game_start(r, 'DMIA')
    play_stage(r)
    leave_stage(r)
    print('run 3 (password stage 3): run bits %d' % r.a.run_bits, r.events)
