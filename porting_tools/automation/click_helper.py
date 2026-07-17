import sys
import time
import Quartz

def click(x, y, click_count=1, button='left'):
    down_type = Quartz.kCGEventLeftMouseDown
    up_type = Quartz.kCGEventLeftMouseUp
    mouse_button = Quartz.kCGMouseButtonLeft
    if button == 'right':
        down_type = Quartz.kCGEventRightMouseDown
        up_type = Quartz.kCGEventRightMouseUp
        mouse_button = Quartz.kCGMouseButtonRight

    move = Quartz.CGEventCreateMouseEvent(None, Quartz.kCGEventMouseMoved, (x, y), mouse_button)
    Quartz.CGEventPost(Quartz.kCGHIDEventTap, move)
    time.sleep(0.05)

    for i in range(click_count):
        down = Quartz.CGEventCreateMouseEvent(None, down_type, (x, y), mouse_button)
        Quartz.CGEventSetIntegerValueField(down, Quartz.kCGMouseEventClickState, i + 1)
        Quartz.CGEventPost(Quartz.kCGHIDEventTap, down)
        time.sleep(0.03)
        up = Quartz.CGEventCreateMouseEvent(None, up_type, (x, y), mouse_button)
        Quartz.CGEventSetIntegerValueField(up, Quartz.kCGMouseEventClickState, i + 1)
        Quartz.CGEventPost(Quartz.kCGHIDEventTap, up)
        time.sleep(0.08)

if __name__ == '__main__':
    x = float(sys.argv[1])
    y = float(sys.argv[2])
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    click(x, y, count)
    print(f"clicked at ({x},{y}) count={count}")
