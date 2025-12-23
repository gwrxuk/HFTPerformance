#!/usr/bin/env python3
"""
Generate HFTPerformance Architecture Diagram using Pillow
Figure 1: System Architecture Overview
"""

from PIL import Image, ImageDraw, ImageFont
import os

# Create output directory
os.makedirs('docs/images', exist_ok=True)

# Canvas settings
WIDTH = 1400
HEIGHT = 1000
BG_COLOR = '#0d1117'  # Dark background (GitHub dark theme)
ACCENT_COLOR = '#58a6ff'  # Blue accent
SECONDARY_COLOR = '#8b949e'  # Gray text
SUCCESS_COLOR = '#3fb950'  # Green
WARNING_COLOR = '#d29922'  # Orange
ERROR_COLOR = '#f85149'  # Red
BOX_BG = '#161b22'  # Dark box background
BOX_BORDER = '#30363d'  # Border color

# Create image
img = Image.new('RGB', (WIDTH, HEIGHT), BG_COLOR)
draw = ImageDraw.Draw(img)

# Try to load a nice font, fallback to default
try:
    title_font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 28)
    header_font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 18)
    text_font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 14)
    small_font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 12)
except:
    title_font = ImageFont.load_default()
    header_font = ImageFont.load_default()
    text_font = ImageFont.load_default()
    small_font = ImageFont.load_default()

def draw_rounded_rect(draw, coords, radius, fill, outline=None, width=1):
    """Draw a rounded rectangle"""
    x1, y1, x2, y2 = coords
    draw.rectangle([x1+radius, y1, x2-radius, y2], fill=fill)
    draw.rectangle([x1, y1+radius, x2, y2-radius], fill=fill)
    draw.ellipse([x1, y1, x1+2*radius, y1+2*radius], fill=fill)
    draw.ellipse([x2-2*radius, y1, x2, y1+2*radius], fill=fill)
    draw.ellipse([x1, y2-2*radius, x1+2*radius, y2], fill=fill)
    draw.ellipse([x2-2*radius, y2-2*radius, x2, y2], fill=fill)
    if outline:
        # Draw outline
        draw.arc([x1, y1, x1+2*radius, y1+2*radius], 180, 270, fill=outline, width=width)
        draw.arc([x2-2*radius, y1, x2, y1+2*radius], 270, 360, fill=outline, width=width)
        draw.arc([x1, y2-2*radius, x1+2*radius, y2], 90, 180, fill=outline, width=width)
        draw.arc([x2-2*radius, y2-2*radius, x2, y2], 0, 90, fill=outline, width=width)
        draw.line([x1+radius, y1, x2-radius, y1], fill=outline, width=width)
        draw.line([x1+radius, y2, x2-radius, y2], fill=outline, width=width)
        draw.line([x1, y1+radius, x1, y2-radius], fill=outline, width=width)
        draw.line([x2, y1+radius, x2, y2-radius], fill=outline, width=width)

def draw_component_box(x, y, w, h, title, items, color, icon="■"):
    """Draw a component box with title and items"""
    draw_rounded_rect(draw, [x, y, x+w, y+h], 8, BOX_BG, color, 2)
    
    # Header bar
    draw.rectangle([x+2, y+2, x+w-2, y+35], fill=color)
    draw.text((x+15, y+8), f"{icon} {title}", fill='white', font=header_font)
    
    # Items
    y_offset = 45
    for item in items:
        draw.text((x+15, y+y_offset), f"• {item}", fill=SECONDARY_COLOR, font=text_font)
        y_offset += 22

def draw_arrow(x1, y1, x2, y2, color=SECONDARY_COLOR, label="", dashed=False):
    """Draw an arrow between two points"""
    if dashed:
        # Draw dashed line
        dash_len = 8
        gap_len = 4
        dx = x2 - x1
        dy = y2 - y1
        length = (dx**2 + dy**2)**0.5
        if length == 0:
            return
        dx, dy = dx/length, dy/length
        pos = 0
        while pos < length - 10:
            end = min(pos + dash_len, length - 10)
            draw.line([x1 + dx*pos, y1 + dy*pos, x1 + dx*end, y1 + dy*end], fill=color, width=2)
            pos += dash_len + gap_len
    else:
        draw.line([x1, y1, x2, y2], fill=color, width=2)
    
    # Arrowhead
    import math
    angle = math.atan2(y2-y1, x2-x1)
    arrow_size = 10
    draw.polygon([
        (x2, y2),
        (x2 - arrow_size * math.cos(angle - 0.4), y2 - arrow_size * math.sin(angle - 0.4)),
        (x2 - arrow_size * math.cos(angle + 0.4), y2 - arrow_size * math.sin(angle + 0.4))
    ], fill=color)
    
    # Label
    if label:
        mid_x = (x1 + x2) / 2
        mid_y = (y1 + y2) / 2 - 10
        draw.text((mid_x - len(label)*3, mid_y), label, fill=color, font=small_font)

# Title
draw.text((WIDTH//2 - 250, 20), "HFTPerformance Architecture", fill='white', font=title_font)
draw.text((WIDTH//2 - 180, 55), "Figure 1: System Components Overview", fill=SECONDARY_COLOR, font=text_font)

# ============ LEFT COLUMN: Input/Config ============
# Config File
draw_component_box(50, 100, 200, 140, "Config Engine", [
    "JSON Parser",
    "Mode Selection",
    "Advanced Options",
    "Validation"
], WARNING_COLOR, "⚙")

# User Strategy
draw_component_box(50, 270, 200, 120, "User Strategy", [
    "onTick() callback",
    "onOrderResponse()",
    "Custom Logic"
], SUCCESS_COLOR, "📊")

# ============ CENTER: Core Components ============
# Market Data Generator
draw_component_box(320, 100, 280, 160, "Market Data Generator", [
    "Tick Generation",
    "Multi-Symbol Round-Robin",
    "Gap Recovery Simulation",
    "Jitter Injection",
    "Rate Control (Poisson/Uniform)"
], ACCENT_COLOR, "📈")

# Matching Engine
draw_component_box(320, 290, 280, 180, "Matching Engine", [
    "Price-Time Priority",
    "Order Book Management",
    "Lock-free Queues",
    "Trade Execution",
    "Fill Simulation",
    "Multi-instrument Support"
], ERROR_COLOR, "⚡")

# Statistics Collector
draw_component_box(320, 500, 280, 140, "Statistics Collector", [
    "Latency Measurement",
    "Percentile Calculation",
    "Throughput Tracking",
    "Warmup Filtering"
], SUCCESS_COLOR, "📉")

# ============ RIGHT COLUMN: Output/Transport ============
# Transport Layer
draw_component_box(670, 100, 220, 140, "Transport Layer", [
    "UDP Multicast",
    "IPC Unix Sockets",
    "Lock-free SPSC Queue",
    "Zero-copy Transfer"
], '#a371f7', "🔌")

# Pipeline Executor
draw_component_box(670, 270, 220, 140, "Pipeline Executor", [
    "Multi-threaded Stages",
    "CPU Affinity Pinning",
    "Busy-wait Polling",
    "Memory Locking"
], WARNING_COLOR, "🔄")

# Output Reporter
draw_component_box(670, 440, 220, 160, "Output Reporter", [
    "Console Progress",
    "CSV Export",
    "Summary Line",
    "Flame Graph (perf)",
    "Detailed Statistics"
], ACCENT_COLOR, "📋")

# ============ BOTTOM: External Systems ============
# External System box (dashed)
draw.rectangle([920, 100, 1350, 300], outline=SECONDARY_COLOR, width=1)
draw.text((940, 110), "External Mode (Optional)", fill=SECONDARY_COLOR, font=header_font)

draw_component_box(940, 140, 180, 70, "Your Trading System", [
    "Custom Strategy"
], '#8b949e', "🖥")

draw_component_box(1140, 140, 180, 70, "External Exchange", [
    "Real/Simulated"
], '#8b949e', "🏦")

# ============ ARROWS ============
# Config -> Market Data Generator
draw_arrow(250, 170, 320, 170, WARNING_COLOR, "config")

# Config -> User Strategy
draw_arrow(150, 240, 150, 270, WARNING_COLOR)

# User Strategy -> Matching Engine
draw_arrow(250, 330, 320, 380, SUCCESS_COLOR, "orders")

# Market Data Generator -> Strategy (ticks)
draw_arrow(320, 200, 250, 290, ACCENT_COLOR, "ticks")

# Market Data Generator -> Matching Engine
draw_arrow(460, 260, 460, 290, ACCENT_COLOR, "ticks")

# Matching Engine -> Statistics
draw_arrow(460, 470, 460, 500, ERROR_COLOR, "latency")

# Matching Engine -> Transport (external)
draw_arrow(600, 380, 670, 340, '#a371f7', "", True)

# Transport -> Pipeline
draw_arrow(780, 240, 780, 270, '#a371f7')

# Pipeline -> Matching Engine
draw_arrow(670, 340, 600, 380, WARNING_COLOR, "orders")

# Statistics -> Output
draw_arrow(600, 570, 670, 520, SUCCESS_COLOR, "stats")

# Transport -> External
draw_arrow(890, 170, 940, 175, '#a371f7', "UDP", True)

# External -> Transport
draw_arrow(940, 195, 890, 190, '#8b949e', "IPC", True)

# ============ LEGEND ============
legend_y = 680
draw.text((50, legend_y), "Legend:", fill='white', font=header_font)

legend_items = [
    (ACCENT_COLOR, "Data Generation"),
    (ERROR_COLOR, "Order Processing"),
    (SUCCESS_COLOR, "Strategy/Stats"),
    (WARNING_COLOR, "Config/Pipeline"),
    ('#a371f7', "Transport"),
    ('#8b949e', "External (Optional)")
]

x_offset = 50
for i, (color, label) in enumerate(legend_items):
    x = x_offset + (i % 3) * 220
    y = legend_y + 30 + (i // 3) * 25
    draw.rectangle([x, y, x+15, y+15], fill=color)
    draw.text((x+25, y), label, fill=SECONDARY_COLOR, font=text_font)

# ============ DATA FLOW SUMMARY ============
summary_y = 780
draw.rectangle([50, summary_y, WIDTH-50, HEIGHT-30], outline=BOX_BORDER, width=1)
draw.text((70, summary_y + 15), "Data Flow:", fill='white', font=header_font)

flow_text = [
    "1. Config Engine loads JSON settings and initializes all components",
    "2. Market Data Generator produces realistic tick data with configurable patterns",
    "3. User Strategy (embedded) or External System processes ticks and generates orders",
    "4. Matching Engine executes orders using price-time priority",
    "5. Statistics Collector measures end-to-end latency at nanosecond precision",
    "6. Output Reporter generates detailed results and optional flame graphs"
]

for i, text in enumerate(flow_text):
    draw.text((70, summary_y + 45 + i * 20), text, fill=SECONDARY_COLOR, font=small_font)

# Save the image
output_path = 'docs/images/architecture_diagram.png'
img.save(output_path, 'PNG', quality=95)
print(f"✓ Architecture diagram saved to: {output_path}")

# Also create a simpler component diagram
img2 = Image.new('RGB', (1200, 600), BG_COLOR)
draw2 = ImageDraw.Draw(img2)

# Title
draw2.text((400, 20), "HFTPerformance Component Flow", fill='white', font=title_font)

# Simple boxes for flow
boxes = [
    (50, 250, "Config\n(.json)", WARNING_COLOR),
    (200, 250, "Market Data\nGenerator", ACCENT_COLOR),
    (400, 250, "Strategy\n(User/Built-in)", SUCCESS_COLOR),
    (600, 250, "Matching\nEngine", ERROR_COLOR),
    (800, 250, "Statistics\nCollector", SUCCESS_COLOR),
    (1000, 250, "Output\n(CSV/Console)", ACCENT_COLOR),
]

for x, y, text, color in boxes:
    draw_rounded_rect(draw2, [x, y, x+140, y+80], 8, BOX_BG, color, 2)
    lines = text.split('\n')
    for i, line in enumerate(lines):
        draw2.text((x + 70 - len(line)*4, y + 25 + i*20), line, fill='white', font=text_font)

# Arrows
for i in range(len(boxes) - 1):
    x1 = boxes[i][0] + 140
    x2 = boxes[i+1][0]
    y = 290
    draw2.line([x1, y, x2, y], fill=SECONDARY_COLOR, width=2)
    draw2.polygon([(x2, y), (x2-8, y-5), (x2-8, y+5)], fill=SECONDARY_COLOR)

# Labels
labels = ["parse", "ticks", "orders", "latency", "stats"]
for i, label in enumerate(labels):
    x = boxes[i][0] + 150
    draw2.text((x, 270), label, fill=SECONDARY_COLOR, font=small_font)

# Bottom note
draw2.text((350, 400), "Latency measured end-to-end: tick arrival → order execution → statistics", 
           fill=SECONDARY_COLOR, font=text_font)

output_path2 = 'docs/images/component_flow.png'
img2.save(output_path2, 'PNG', quality=95)
print(f"✓ Component flow diagram saved to: {output_path2}")

print("\nDiagrams generated successfully!")

