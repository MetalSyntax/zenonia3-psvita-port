import sys, Quartz
x, y = float(sys.argv[1]), float(sys.argv[2])
up = Quartz.CGEventCreateMouseEvent(None, Quartz.kCGEventLeftMouseUp, (x, y), Quartz.kCGMouseButtonLeft)
Quartz.CGEventPost(Quartz.kCGHIDEventTap, up)
print("mouse up")
