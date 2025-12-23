#!/usr/bin/env python3
"""
Generate HFTPerformance Detailed Architecture Diagram using Pillow
Figure 2: Thread Interactions, Timing, and Data Flow
"""

from PIL import Image, ImageDraw, ImageFont
import os
import math

# Create output directory
os.makedirs('docs/images', exist_ok=True)

# Canvas settings
WIDTH = 1600
HEIGHT = 1400
BG_COLOR = '#0d1117'
ACCENT_COLOR = '#58a6ff'
SECONDARY_COLOR = '#8b949e'
SUCCESS_COLOR = '#3fb950'
WARNING_COLOR = '#d29922'
ERROR_COLOR = '#f85149'
BOX_BG = '#161b22'
BOX_BORDER = '#30363d'
PURPLE = '#a371f7'
CYAN = '#56d4dd'

img = Image.new('RGB', (WIDTH, HEIGHT), BG_COLOR)
draw = ImageDraw.Draw(img)

try:
    title_font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 28)
    header_font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 16)
    text_font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 13)
    small_font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 11)
    code_font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 11)
except:
    title_font = header_font = text_font = small_font = code_font = ImageFont.load_default()

def draw_rounded_rect(coords, radius, fill, outline=None, width=1):
    x1, y1, x2, y2 = coords
    draw.rectangle([x1+radius, y1, x2-radius, y2], fill=fill)
    draw.rectangle([x1, y1+radius, x2, y2-radius], fill=fill)
    draw.ellipse([x1, y1, x1+2*radius, y1+2*radius], fill=fill)
    draw.ellipse([x2-2*radius, y1, x2, y1+2*radius], fill=fill)
    draw.ellipse([x1, y2-2*radius, x1+2*radius, y2], fill=fill)
    draw.ellipse([x2-2*radius, y2-2*radius, x2, y2], fill=fill)
    if outline:
        draw.arc([x1, y1, x1+2*radius, y1+2*radius], 180, 270, fill=outline, width=width)
        draw.arc([x2-2*radius, y1, x2, y1+2*radius], 270, 360, fill=outline, width=width)
        draw.arc([x1, y2-2*radius, x1+2*radius, y2], 90, 180, fill=outline, width=width)
        draw.arc([x2-2*radius, y2-2*radius, x2, y2], 0, 90, fill=outline, width=width)
        draw.line([x1+radius, y1, x2-radius, y1], fill=outline, width=width)
        draw.line([x1+radius, y2, x2-radius, y2], fill=outline, width=width)
        draw.line([x1, y1+radius, x1, y2-radius], fill=outline, width=width)
        draw.line([x2, y1+radius, x2, y2-radius], fill=outline, width=width)

def draw_arrow(x1, y1, x2, y2, color=SECONDARY_COLOR, label="", dashed=False):
    if dashed:
        dash_len, gap_len = 6, 4
        dx, dy = x2 - x1, y2 - y1
        length = (dx**2 + dy**2)**0.5
        if length == 0: return
        dx, dy = dx/length, dy/length
        pos = 0
        while pos < length - 10:
            end = min(pos + dash_len, length - 10)
            draw.line([x1 + dx*pos, y1 + dy*pos, x1 + dx*end, y1 + dy*end], fill=color, width=2)
            pos += dash_len + gap_len
    else:
        draw.line([x1, y1, x2, y2], fill=color, width=2)
    
    angle = math.atan2(y2-y1, x2-x1)
    arrow_size = 8
    draw.polygon([
        (x2, y2),
        (x2 - arrow_size * math.cos(angle - 0.4), y2 - arrow_size * math.sin(angle - 0.4)),
        (x2 - arrow_size * math.cos(angle + 0.4), y2 - arrow_size * math.sin(angle + 0.4))
    ], fill=color)
    
    if label:
        mid_x, mid_y = (x1 + x2) / 2, (y1 + y2) / 2 - 8
        draw.text((mid_x - len(label)*3, mid_y), label, fill=color, font=small_font)

def draw_thread_box(x, y, w, h, title, cpu_core, items, color):
    """Draw a thread box with CPU core indicator"""
    draw_rounded_rect([x, y, x+w, y+h], 6, BOX_BG, color, 2)
    draw.rectangle([x+2, y+2, x+w-2, y+28], fill=color)
    draw.text((x+10, y+6), title, fill='white', font=header_font)
    
    # CPU core badge
    if cpu_core is not None:
        badge_x = x + w - 60
        draw.ellipse([badge_x, y+5, badge_x+50, y+23], fill='#21262d', outline=CYAN)
        draw.text((badge_x+8, y+7), f"CPU {cpu_core}", fill=CYAN, font=small_font)
    
    y_off = 35
    for item in items:
        draw.text((x+10, y+y_off), f"• {item}", fill=SECONDARY_COLOR, font=text_font)
        y_off += 18

def draw_queue(x, y, w, h, label, size="64K"):
    """Draw a lock-free queue representation"""
    # Queue body
    draw.rectangle([x, y, x+w, y+h], fill='#1c2128', outline=PURPLE, width=2)
    
    # Queue slots
    slot_w = w // 8
    for i in range(8):
        color = SUCCESS_COLOR if i < 5 else '#21262d'
        draw.rectangle([x+i*slot_w+2, y+2, x+(i+1)*slot_w-2, y+h-2], fill=color)
    
    # Labels
    draw.text((x, y-15), label, fill=PURPLE, font=small_font)
    draw.text((x+w+5, y+h//2-6), size, fill=SECONDARY_COLOR, font=small_font)

def draw_timing_block(x, y, w, h):
    """Draw timing mechanism detail"""
    draw_rounded_rect([x, y, x+w, y+h], 6, '#1c2128', CYAN, 2)
    draw.text((x+10, y+8), "⏱ High-Resolution Timing", fill=CYAN, font=header_font)
    
    code_lines = [
        "rdtsc / clock_gettime(CLOCK_MONOTONIC)",
        "atomic_thread_fence(memory_order_seq_cst)",
        "Timestamp t0 = now();  // ~15ns overhead",
        "/* critical path */",
        "Timestamp t1 = now();",
        "latency_ns = t1 - t0;  // nanosecond precision"
    ]
    y_off = 35
    for line in code_lines:
        draw.text((x+15, y+y_off), line, fill='#7ee787', font=code_font)
        y_off += 16

def draw_memory_layout(x, y, w, h):
    """Draw cache-aligned memory layout"""
    draw_rounded_rect([x, y, x+w, y+h], 6, '#1c2128', WARNING_COLOR, 2)
    draw.text((x+10, y+8), "🧠 Cache-Aligned Memory Layout", fill=WARNING_COLOR, font=header_font)
    
    # Cache lines
    line_h = 25
    labels = [
        ("Order struct", "64B aligned", SUCCESS_COLOR),
        ("SPSC Queue head", "64B padding", ACCENT_COLOR),
        ("SPSC Queue tail", "64B padding", ACCENT_COLOR),
        ("Price Level", "128B aligned", ERROR_COLOR),
    ]
    y_off = 38
    for label, size, color in labels:
        draw.rectangle([x+15, y+y_off, x+w-80, y+y_off+line_h-4], fill=color, outline='white')
        draw.text((x+20, y+y_off+4), label, fill='white', font=small_font)
        draw.text((x+w-75, y+y_off+4), size, fill=SECONDARY_COLOR, font=small_font)
        y_off += line_h

# ============ TITLE ============
draw.text((WIDTH//2 - 320, 15), "HFTPerformance Detailed Architecture", fill='white', font=title_font)
draw.text((WIDTH//2 - 280, 50), "Figure 2: Thread Interactions, Timing, and Data Flow", fill=SECONDARY_COLOR, font=text_font)

# ============ SECTION 1: THREAD MODEL ============
section_y = 90
draw.text((50, section_y), "Thread Model & CPU Affinity", fill='white', font=header_font)
draw.line([50, section_y+22, 350, section_y+22], fill=ACCENT_COLOR, width=2)

# Thread 0: Generator
draw_thread_box(50, section_y+35, 280, 180, "Thread 0: Generator", 0, [
    "Market data generation",
    "Tick timestamping (T0)",
    "Gap recovery injection",
    "Jitter simulation",
    "Rate control loop",
    "Warmup period tracking"
], ACCENT_COLOR)

# Thread 1: Processor  
draw_thread_box(370, section_y+35, 280, 180, "Thread 1: Processor", 1, [
    "Order book matching",
    "Lock-free queue drain",
    "Fill simulation",
    "Latency measurement (T1)",
    "Statistics aggregation",
    "Busy-wait polling"
], ERROR_COLOR)

# Thread 2: Reporter (optional)
draw_thread_box(690, section_y+35, 280, 180, "Thread 2: Reporter", 2, [
    "Progress output",
    "CSV logging",
    "Percentile calculation",
    "Summary generation",
    "(Lower priority)",
    "Non-critical path"
], SUCCESS_COLOR)

# Queue between threads
draw_queue(330, section_y+230, 120, 25, "SPSC Queue")
draw_arrow(280, section_y+145, 330, section_y+242, PURPLE, "enqueue")
draw_arrow(450, section_y+242, 420, section_y+145, PURPLE, "dequeue")

# ============ SECTION 2: LOCK-FREE SYNCHRONIZATION ============
section_y = 320
draw.text((50, section_y), "Lock-Free Synchronization", fill='white', font=header_font)
draw.line([50, section_y+22, 350, section_y+22], fill=ACCENT_COLOR, width=2)

sync_box_y = section_y + 35
draw_rounded_rect([50, sync_box_y, 450, sync_box_y+200], 6, BOX_BG, PURPLE, 2)
draw.text((60, sync_box_y+10), "SPSC Queue Implementation", fill=PURPLE, font=header_font)

spsc_code = [
    "template<typename T, size_t Capacity>",
    "class SPSCQueue {",
    "  alignas(64) atomic<size_t> head_;  // Producer",
    "  alignas(64) atomic<size_t> tail_;  // Consumer", 
    "  alignas(64) size_t cached_head_;   // Local cache",
    "  alignas(64) size_t cached_tail_;   // Local cache",
    "  T buffer_[Capacity];",
    "  ",
    "  bool try_push(T& item) {",
    "    // memory_order_relaxed for hot path",
    "    // memory_order_release on commit",
    "  }",
    "};"
]
y_off = sync_box_y + 35
for line in spsc_code:
    draw.text((65, y_off), line, fill='#7ee787', font=code_font)
    y_off += 14

# Spinlock box
draw_rounded_rect([480, sync_box_y, 780, sync_box_y+200], 6, BOX_BG, WARNING_COLOR, 2)
draw.text((490, sync_box_y+10), "Adaptive Spinlock", fill=WARNING_COLOR, font=header_font)

spin_code = [
    "class Spinlock {",
    "  atomic<bool> locked_{false};",
    "  ",
    "  void lock() {",
    "    // Fast path: single CAS",
    "    if (!locked_.exchange(true,",
    "        memory_order_acquire))",
    "      return;",
    "    // Slow path: exponential backoff",
    "    lock_slow();  // PAUSE + yield",
    "  }",
    "};"
]
y_off = sync_box_y + 35
for line in spin_code:
    draw.text((495, y_off), line, fill='#7ee787', font=code_font)
    y_off += 14

# ============ SECTION 3: TIMING PRECISION ============
section_y = 560
draw.text((50, section_y), "Timing & Measurement Precision", fill='white', font=header_font)
draw.line([50, section_y+22, 380, section_y+22], fill=ACCENT_COLOR, width=2)

draw_timing_block(50, section_y+35, 380, 130)

# Measurement points diagram
meas_y = section_y + 35
meas_x = 480
draw_rounded_rect([meas_x, meas_y, meas_x+500, meas_y+130], 6, '#1c2128', CYAN, 2)
draw.text((meas_x+10, meas_y+8), "📊 Latency Measurement Points", fill=CYAN, font=header_font)

# Timeline
timeline_y = meas_y + 50
draw.line([meas_x+30, timeline_y, meas_x+470, timeline_y], fill=SECONDARY_COLOR, width=2)

points = [
    (meas_x+50, "T0: Tick\nGenerated", ACCENT_COLOR),
    (meas_x+150, "T1: Queue\nEnqueue", PURPLE),
    (meas_x+250, "T2: Queue\nDequeue", PURPLE),
    (meas_x+350, "T3: Match\nComplete", ERROR_COLOR),
    (meas_x+450, "T4: Stats\nRecorded", SUCCESS_COLOR),
]

for px, label, color in points:
    draw.ellipse([px-6, timeline_y-6, px+6, timeline_y+6], fill=color)
    lines = label.split('\n')
    for i, l in enumerate(lines):
        draw.text((px-25, timeline_y+12+i*12), l, fill=SECONDARY_COLOR, font=small_font)

# Latency spans
draw.line([meas_x+50, timeline_y-15, meas_x+350, timeline_y-15], fill=ERROR_COLOR, width=2)
draw.text((meas_x+170, timeline_y-28), "End-to-End Latency", fill=ERROR_COLOR, font=small_font)

# ============ SECTION 4: MEMORY LAYOUT ============
section_y = 720
draw.text((50, section_y), "Memory Layout & Cache Optimization", fill='white', font=header_font)
draw.line([50, section_y+22, 420, section_y+22], fill=ACCENT_COLOR, width=2)

draw_memory_layout(50, section_y+35, 350, 140)

# False sharing prevention
fs_x = 430
draw_rounded_rect([fs_x, section_y+35, fs_x+350, section_y+175], 6, '#1c2128', ERROR_COLOR, 2)
draw.text((fs_x+10, section_y+43), "🚫 False Sharing Prevention", fill=ERROR_COLOR, font=header_font)

fs_items = [
    "• Each atomic variable on separate cache line",
    "• alignas(64) / alignas(128) annotations",
    "• Producer/Consumer data isolated",
    "• Hot data packed together",
    "• Cold data (stats) on separate lines",
    "• Memory prefetching hints (likely/unlikely)"
]
y_off = section_y + 70
for item in fs_items:
    draw.text((fs_x+15, y_off), item, fill=SECONDARY_COLOR, font=text_font)
    y_off += 18

# ============ SECTION 5: DATA FLOW DETAIL ============
section_y = 910
draw.text((50, section_y), "Detailed Data Flow (Pipeline Mode)", fill='white', font=header_font)
draw.line([50, section_y+22, 420, section_y+22], fill=ACCENT_COLOR, width=2)

flow_y = section_y + 40

# Flow boxes
flow_boxes = [
    (50, "Config\nParse", WARNING_COLOR, 80),
    (160, "Symbol\nSetup", WARNING_COLOR, 80),
    (270, "Tick\nGenerate", ACCENT_COLOR, 80),
    (380, "Timestamp\nT0", CYAN, 80),
    (490, "Queue\nPush", PURPLE, 80),
    (600, "Queue\nPop", PURPLE, 80),
    (710, "Order\nMatch", ERROR_COLOR, 80),
    (820, "Timestamp\nT1", CYAN, 80),
    (930, "Stats\nRecord", SUCCESS_COLOR, 80),
    (1040, "Report\nOutput", SUCCESS_COLOR, 80),
]

for x, label, color, w in flow_boxes:
    draw_rounded_rect([x, flow_y, x+w, flow_y+50], 4, BOX_BG, color, 2)
    lines = label.split('\n')
    for i, l in enumerate(lines):
        draw.text((x+w//2-len(l)*3, flow_y+10+i*14), l, fill='white', font=small_font)

# Arrows between flow boxes
for i in range(len(flow_boxes)-1):
    x1 = flow_boxes[i][0] + flow_boxes[i][3]
    x2 = flow_boxes[i+1][0]
    draw_arrow(x1, flow_y+25, x2, flow_y+25, SECONDARY_COLOR)

# Thread ownership labels
draw.text((50, flow_y+60), "──── Thread 0 (Generator) ────", fill=ACCENT_COLOR, font=small_font)
draw.text((600, flow_y+60), "──── Thread 1 (Processor) ────", fill=ERROR_COLOR, font=small_font)

# ============ SECTION 6: PERFORMANCE OPTIMIZATIONS ============
section_y = 1030
draw.text((50, section_y), "Performance Optimizations", fill='white', font=header_font)
draw.line([50, section_y+22, 320, section_y+22], fill=ACCENT_COLOR, width=2)

opt_boxes = [
    (50, "CPU Affinity", [
        "pthread_setaffinity_np()",
        "Isolate threads to cores",
        "Prevent migration overhead",
        "NUMA-aware allocation"
    ], ACCENT_COLOR),
    (300, "Memory Locking", [
        "mlockall(MCL_CURRENT)",
        "mlockall(MCL_FUTURE)",
        "Prevent page faults",
        "Pre-fault allocations"
    ], SUCCESS_COLOR),
    (550, "Busy-Wait Polling", [
        "_mm_pause() / yield",
        "Exponential backoff",
        "No syscall overhead",
        "Sub-microsecond wake"
    ], WARNING_COLOR),
    (800, "Branch Prediction", [
        "__builtin_expect()",
        "likely() / unlikely()",
        "Hot path optimization",
        "Profile-guided layout"
    ], ERROR_COLOR),
]

for x, title, items, color in opt_boxes:
    draw_rounded_rect([x, section_y+35, x+220, section_y+150], 6, BOX_BG, color, 2)
    draw.rectangle([x+2, section_y+37, x+218, section_y+57], fill=color)
    draw.text((x+10, section_y+40), title, fill='white', font=header_font)
    y_off = section_y + 65
    for item in items:
        draw.text((x+10, y_off), f"• {item}", fill=SECONDARY_COLOR, font=small_font)
        y_off += 18

# ============ LEGEND ============
legend_y = 1200
draw.text((50, legend_y), "Legend:", fill='white', font=header_font)

legend_items = [
    (ACCENT_COLOR, "Data Generation"),
    (ERROR_COLOR, "Order Processing"),
    (SUCCESS_COLOR, "Statistics/Output"),
    (WARNING_COLOR, "Config/Control"),
    (PURPLE, "Lock-free Queues"),
    (CYAN, "Timing/Measurement"),
]

for i, (color, label) in enumerate(legend_items):
    x = 50 + (i % 3) * 200
    y = legend_y + 25 + (i // 3) * 22
    draw.rectangle([x, y, x+15, y+15], fill=color)
    draw.text((x+22, y), label, fill=SECONDARY_COLOR, font=text_font)

# ============ KEY METRICS ============
metrics_x = 700
draw_rounded_rect([metrics_x, legend_y, metrics_x+450, legend_y+70], 6, '#1c2128', CYAN, 2)
draw.text((metrics_x+15, legend_y+10), "Key Performance Metrics", fill=CYAN, font=header_font)
metrics = [
    "• Timing overhead: ~15-20ns per measurement",
    "• Queue latency: ~50-100ns (cache-hot)",
    "• Context switch avoidance: busy-wait polling",
]
y_off = legend_y + 32
for m in metrics:
    draw.text((metrics_x+15, y_off), m, fill=SECONDARY_COLOR, font=small_font)
    y_off += 14

# ============ FOOTER ============
draw.text((WIDTH//2 - 200, HEIGHT-30), 
          "HFTPerformance v1.0 - Low-Latency Performance Testing Framework", 
          fill=SECONDARY_COLOR, font=small_font)

# Save
output_path = 'docs/images/detailed_architecture.png'
img.save(output_path, 'PNG', quality=95)
print(f"✓ Detailed architecture diagram saved to: {output_path}")

# ============ CREATE THREAD INTERACTION SEQUENCE DIAGRAM ============
img2 = Image.new('RGB', (1400, 800), BG_COLOR)
draw2 = ImageDraw.Draw(img2)

draw2.text((450, 20), "Thread Interaction Sequence", fill='white', font=title_font)
draw2.text((480, 55), "Figure 2b: Message Passing Timeline", fill=SECONDARY_COLOR, font=text_font)

# Thread lifelines
threads = [
    (150, "Main Thread\n(Generator)", ACCENT_COLOR),
    (450, "Worker Thread\n(Processor)", ERROR_COLOR),
    (750, "Stats Thread\n(Reporter)", SUCCESS_COLOR),
    (1050, "SPSC Queue", PURPLE),
]

for x, label, color in threads:
    draw2.rectangle([x-40, 100, x+40, 130], fill=color)
    lines = label.split('\n')
    for i, l in enumerate(lines):
        draw2.text((x-len(l)*3.5, 105+i*12), l, fill='white', font=small_font)
    draw2.line([x, 130, x, 700], fill=color, width=2)

# Sequence messages
messages = [
    (150, 1050, 160, "push(tick)", PURPLE),
    (1050, 450, 200, "pop() → tick", PURPLE),
    (450, 450, 240, "match(order)", ERROR_COLOR),
    (450, 750, 280, "record(latency)", SUCCESS_COLOR),
    (150, 1050, 320, "push(tick)", PURPLE),
    (1050, 450, 360, "pop() → tick", PURPLE),
    (450, 450, 400, "match(order)", ERROR_COLOR),
    (750, 750, 440, "aggregate()", SUCCESS_COLOR),
    (150, 1050, 480, "push(tick)", PURPLE),
    (150, 150, 520, "check_gap_recovery()", WARNING_COLOR),
    (150, 1050, 560, "burst_push(1000)", WARNING_COLOR),
    (1050, 450, 600, "drain_queue()", PURPLE),
    (750, 750, 640, "report_progress()", SUCCESS_COLOR),
]

for x1, x2, y, label, color in messages:
    if x1 == x2:  # Self-call
        draw2.arc([x1, y-10, x1+40, y+20], 270, 90, fill=color, width=2)
        draw2.text((x1+45, y-5), label, fill=color, font=small_font)
    else:
        draw2.line([x1, y, x2, y], fill=color, width=2)
        # Arrow head
        if x2 > x1:
            draw2.polygon([(x2, y), (x2-8, y-4), (x2-8, y+4)], fill=color)
            draw2.text(((x1+x2)//2-len(label)*3, y-15), label, fill=color, font=small_font)
        else:
            draw2.polygon([(x2, y), (x2+8, y-4), (x2+8, y+4)], fill=color)
            draw2.text(((x1+x2)//2-len(label)*3, y-15), label, fill=color, font=small_font)

# Time annotations
draw2.text((50, 160), "T=0", fill=SECONDARY_COLOR, font=small_font)
draw2.text((50, 320), "T=10µs", fill=SECONDARY_COLOR, font=small_font)
draw2.text((50, 480), "T=20µs", fill=SECONDARY_COLOR, font=small_font)
draw2.text((50, 640), "T=30µs", fill=SECONDARY_COLOR, font=small_font)

# Notes
draw2.text((100, 720), "Note: Lock-free queue enables zero-blocking communication between threads", 
           fill=SECONDARY_COLOR, font=text_font)

output_path2 = 'docs/images/thread_sequence.png'
img2.save(output_path2, 'PNG', quality=95)
print(f"✓ Thread sequence diagram saved to: {output_path2}")

print("\nAll detailed diagrams generated successfully!")

