// Public Domain License 2016
//
// Simulate right-handed unix/linux X11 middle-mouse-click copy and paste.
//
// References:
// http://stackoverflow.com/questions/3134901/mouse-tracking-daemon
// http://stackoverflow.com/questions/2379867/simulating-key-press-events-in-mac-os-x#2380280
//
// Compile with:
// gcc -framework ApplicationServices -o macpaste macpaste.c
//
// Start with:
// ./macpaste
//
// Terminate with Ctrl+C

#include <stdbool.h>
#include <sys/time.h> // gettimeofday
#include <search.h>

#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h> // kVK_ANSI_*

char gIsDragging = 0;
long long gPrevClickTime = 0;
long long gCurClickTime = 0;
bool gCanSkip = false;
bool gFocusOnMiddleClick = false;

CGEventTapLocation gTapA = kCGAnnotatedSessionEventTap;
CGEventTapLocation gTapH = kCGHIDEventTap;
int gCommandKey = kCGEventFlagMaskCommand;

#define DOUBLE_CLICK_MILLIS 500

long long now() {
    struct timeval te;
    gettimeofday(&te, NULL);
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // caculate milliseconds
    return milliseconds;
}

static bool isSkipWindow(CGPoint *mouse) {
    if (!gCanSkip) {
        return false;
    }
    char buffer[400];
    int layer;
    ENTRY e;
    CFArrayRef windowList = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly,
                                                       kCGNullWindowID);
    CFIndex numWindows = CFArrayGetCount(windowList);
    for(int i = 0; i < (int)numWindows; i++) {
        CFDictionaryRef info = (CFDictionaryRef)CFArrayGetValueAtIndex(windowList, i);
        CFStringRef appName = (CFStringRef)CFDictionaryGetValue(info, kCGWindowOwnerName);
        CFNumberGetValue(
            (CFNumberRef)CFDictionaryGetValue(info, kCGWindowLayer),
            kCFNumberIntType, &layer);
        if (appName != 0) {
            CFStringGetCString(appName, buffer, 400, kCFStringEncodingUTF8);
            e.key = buffer;
            if (NULL == hsearch(e, FIND)) {
                continue;
            }
            if (layer == 0) {
                CGRect rect;
                CFDictionaryRef bounds = (CFDictionaryRef)CFDictionaryGetValue(info,
                                                                               kCGWindowBounds);
                if(bounds) {
                    CGRectMakeWithDictionaryRepresentation(bounds, &rect);
                    if (mouse->x >= rect.origin.x && 
                        mouse->y >= rect.origin.y &&
                        mouse->x < rect.origin.x + rect.size.width &&
                        mouse->y < rect.origin.y + rect.size.height) {
                        CFRelease(windowList);
                        printf("Skipping '%s'\n", buffer);
                        return true;
                    }
                }
            }
        }
    }
    CFRelease(windowList);
    return false;
}

static void paste(CGEventRef event) {
    // Mouse click to focus and position insertion cursor.
    CGPoint mouseLocation = CGEventGetLocation(event);
    if (isSkipWindow(&mouseLocation)) {
        return;
    }
    if (gFocusOnMiddleClick) {
        CGEventRef mouseClickDown = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseDown,
                                                            mouseLocation,
                                                            kCGMouseButtonLeft);
        CGEventRef mouseClickUp = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseUp, mouseLocation,
                                                          kCGMouseButtonLeft);
        CGEventPost(gTapH, mouseClickDown);
        CGEventPost(gTapH, mouseClickUp);
        CFRelease(mouseClickDown);
        CFRelease(mouseClickUp);
    }

    // Allow click events time to position cursor before pasting.
    usleep(1000);

    // Paste.
    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);
    CGEventRef kbdEventPasteDown = CGEventCreateKeyboardEvent(source, kVK_ANSI_V, 1);
    CGEventRef kbdEventPasteUp   = CGEventCreateKeyboardEvent(source, kVK_ANSI_V, 0);
    //CGEventSetFlags( kbdEventPasteDown, kCGEventFlagMaskCommand );
    CGEventSetFlags(kbdEventPasteDown, gCommandKey);
    CGEventPost(gTapA, kbdEventPasteDown);
    CGEventPost(gTapA, kbdEventPasteUp);
    CFRelease(kbdEventPasteDown);
    CFRelease(kbdEventPasteUp);
    CFRelease(source);
}

static void copy(CGEventRef event) {
    CGPoint mouseLocation = CGEventGetLocation(event);
    if (isSkipWindow(&mouseLocation)) {
        return;
    }

    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);
    CGEventRef kbdEventDown = CGEventCreateKeyboardEvent(source, kVK_ANSI_C, 1);
    CGEventRef kbdEventUp   = CGEventCreateKeyboardEvent(source, kVK_ANSI_C, 0);
    //CGEventSetFlags(kbdEventDown, kCGEventFlagMaskCommand);
    CGEventSetFlags(kbdEventDown, gCommandKey);
    CGEventPost(gTapA, kbdEventDown);
    CGEventPost(gTapA, kbdEventUp);
    CFRelease(kbdEventDown);
    CFRelease(kbdEventUp);
    CFRelease(source);
}

static void recordClickTime() {
    gPrevClickTime = gCurClickTime;
    gCurClickTime = now();
}

static char isDoubleClickSpeed() {
    return (gCurClickTime - gPrevClickTime) < DOUBLE_CLICK_MILLIS;
}

static char isDoubleClick() {
    return isDoubleClickSpeed();
}

static CGEventRef mouseCallback (
    CGEventTapProxy proxy,
    CGEventType type,
    CGEventRef event,
    void * refcon
) {
    switch (type) {
    case kCGEventOtherMouseDown:
        paste(event);
        break;

    case kCGEventLeftMouseDown:
        recordClickTime();
        break;

    case kCGEventLeftMouseUp:
        if (isDoubleClick() || gIsDragging) {
            copy(event);
        }
        gIsDragging = 0;
        break;

    case kCGEventLeftMouseDragged:
        gIsDragging = 1;
        break;

    default:
        break;
    }

    // Pass on the event, we must not modify it anyway, we are a listener
    return event;
}

int main (int argc, char **argv) {
    CGEventMask emask;
    CFMachPortRef myEventTap;
    CFRunLoopSourceRef eventTapRLSrc;

    if (argc > 1) {
        if (0 == hcreate((argc - 1)/2)) {
            printf("Can't create hash table\n");
            return -1;
        }
        int opt;
        ENTRY e;
        while ((opt = getopt(argc, argv, "cfs:")) != -1) {
            switch (opt) {
            case 'c':
                gCommandKey = kCGEventFlagMaskControl;
                printf("Using ctrl instead of cmd\n");
                break;

            case 'f':
                gFocusOnMiddleClick = true;
                printf("Will focus on middle click\n");
                break;

            case 's':
                gCanSkip = true;
                printf("Will skip windows named '%s'\n", optarg);
                e.key = optarg;
                e.data = optarg;
                if (NULL == hsearch(e, ENTER)) {
                    printf("Couldn't add skip '%s'\n", optarg);
                    return -1;
                }
                break;
            }
        }
    }

    printf("Quit from command-line foreground with Ctrl+C\n");

    // We want "other" mouse button click-release, such as middle or exotic.
    emask = CGEventMaskBit(kCGEventOtherMouseDown)  |
            CGEventMaskBit(kCGEventLeftMouseDown) |
            CGEventMaskBit(kCGEventLeftMouseUp)   |
            CGEventMaskBit(kCGEventLeftMouseDragged);

    // Create the Tap
    myEventTap = CGEventTapCreate(
                     kCGSessionEventTap,          // Catch all events for current user session
                     kCGTailAppendEventTap,       // Append to end of EventTap list
                     kCGEventTapOptionListenOnly, // We only listen, we don't modify
                     emask,
                     & mouseCallback,
                     NULL                         // We need no extra data in the callback
                 );

    // Create a RunLoop Source for it
    eventTapRLSrc = CFMachPortCreateRunLoopSource(
                        kCFAllocatorDefault,
                        myEventTap,
                        0
                    );

    // Add the source to the current RunLoop
    CFRunLoopAddSource(
        CFRunLoopGetCurrent(),
        eventTapRLSrc,
        kCFRunLoopDefaultMode
    );

    // Keep the RunLoop running forever
    CFRunLoopRun();

    // Not reached (RunLoop above never stops running)
    return 0;
}
