import sys
import time
import Quartz

# macOS virtual keycodes for common keys
KEYCODES = {
    'a': 0, 's': 1, 'd': 2, 'f': 3, 'h': 4, 'g': 5, 'z': 6, 'x': 7, 'c': 8, 'v': 9,
    'b': 11, 'q': 12, 'w': 13, 'e': 14, 'r': 15, 'y': 16, 't': 17,
    '1': 18, '2': 19, '3': 20, '4': 21, '6': 22, '5': 23, '9': 25, '7': 26, '8': 28, '0': 29,
    'return': 36, 'tab': 48, 'space': 49, 'escape': 53,
}

def key_press(key, hold_sec=0.05):
    code = KEYCODES[key.lower()]
    down = Quartz.CGEventCreateKeyboardEvent(None, code, True)
    Quartz.CGEventPost(Quartz.kCGHIDEventTap, down)
    time.sleep(hold_sec)
    up = Quartz.CGEventCreateKeyboardEvent(None, code, False)
    Quartz.CGEventPost(Quartz.kCGHIDEventTap, up)

if __name__ == '__main__':
    key_press(sys.argv[1], float(sys.argv[2]) if len(sys.argv) > 2 else 0.05)
    print(f"pressed key {sys.argv[1]}")
