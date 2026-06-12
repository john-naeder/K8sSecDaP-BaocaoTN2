#!/usr/bin/env python3
"""
Zero-Trust Network Mapper — Multi-Panel Dashboard

Renders a 4-panel dashboard from pipeline output:
    (a) Zone-aggregated graph — CIDR zones as super-nodes, edge weight = flow count
    (b) Alert-focused graph  — only alert sources + their blast radius
    (c) Alerts timeline      — scatter of alert events over time
    (d) Top talkers          — bar chart of IPs with highest out-degree

Usage:
    python graph_renderer.py [--graph PATH] [--alerts PATH] [--stats PATH] [--output PATH]
"""

import argparse
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

try:
    import networkx as nx
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import matplotlib.patches as mpatches
    from matplotlib.gridspec import GridSpec
except ImportError as e:
    print(f"Error: Missing dependency — {e}", file=sys.stderr)
    print("Install: pip install networkx matplotlib", file=sys.stderr)
    sys.exit(1)


# ─── Classification ────────────────────────────────────────────────────────

def classify_ip(ip_str):
    """Return (zone, subzone) — e.g., ('pod', '10.244.1.x')."""
    parts = ip_str.split(".")
    if len(parts) != 4:
        return ("unknown", ip_str)

    first, second, third = int(parts[0]), int(parts[1]), int(parts[2])

    if first == 10 and second == 244:
        return ("pod", f"10.244.{third}.x")
    elif first == 10 and second >= 96:
        return ("service", f"10.{second}.{third}.x")
    elif first == 192 and second == 168:
        return ("node", f"192.168.{third}.x")
    elif first == 172 and 16 <= second <= 31:
        return ("private", f"172.{second}.{third}.x")
    elif first == 127:
        return ("loopback", "127.x.x.x")
    else:
        return ("external", f"{first}.{second}.x.x")


# Zone → color
ZONE_COLORS = {
    "pod":      "#4CAF50",
    "service":  "#2196F3",
    "node":     "#FF9800",
    "private":  "#9C27B0",
    "external": "#F44336",
    "loopback": "#607D8B",
    "unknown":  "#BDBDBD",
}

# Alert type → color
ALERT_COLORS = {
    "port_scan":    "#F57F17",
    "blast_radius": "#D32F2F",
    "scc_anomaly":  "#6A1B9A",
    "architecture": "#C62828",
}


# ─── Data loading ──────────────────────────────────────────────────────────

def load_json(path):
    try:
        with open(path) as f:
            content = f.read().strip()
            if not content:
                return {}
            try:
                return json.loads(content)
            except json.JSONDecodeError:
                pass
            return [json.loads(line) for line in content.split("\n") if line.strip()]
    except (FileNotFoundError, json.JSONDecodeError) as e:
        print(f"Warning: Could not load {path}: {e}", file=sys.stderr)
        return {}


# ─── Panel (a): Zone-Aggregated Graph ──────────────────────────────────────

def panel_zone_aggregated(ax, graph_data):
    """Aggregate IPs by subzone (/24); draw weighted super-graph."""
    G = nx.DiGraph()
    zone_of = {}

    # Collect all subzones
    for v in graph_data.get("vertices", []):
        zone, sub = classify_ip(v)
        zone_of[v] = (zone, sub)
        if sub not in G:
            G.add_node(sub, zone=zone, members=set())
        G.nodes[sub]["members"].add(v)

    # Aggregate edges
    edge_weights = defaultdict(int)
    for e in graph_data.get("edges", []):
        _, src_sub = zone_of.get(e["from"], ("unknown", e["from"]))
        _, dst_sub = zone_of.get(e["to"], ("unknown", e["to"]))
        if src_sub != dst_sub:
            edge_weights[(src_sub, dst_sub)] += 1

    for (u, v), w in edge_weights.items():
        G.add_edge(u, v, weight=w)

    if not G.nodes:
        ax.text(0.5, 0.5, "No data", ha="center", va="center", fontsize=14)
        ax.axis("off")
        ax.set_title("(a) Zone-Aggregated Graph", fontsize=12, fontweight="bold")
        return

    # Layout — circular with zones grouped
    pos = nx.circular_layout(G)

    # Node styling: size ∝ member count
    node_colors = [ZONE_COLORS.get(G.nodes[n]["zone"], "#BDBDBD") for n in G.nodes]
    node_sizes = [min(200 + len(G.nodes[n]["members"]) * 30, 2500) for n in G.nodes]

    # Edge styling: width ∝ log(weight)
    import math
    max_w = max((d["weight"] for _, _, d in G.edges(data=True)), default=1)
    edge_widths = [0.5 + 3.0 * math.log1p(d["weight"]) / math.log1p(max_w)
                   for _, _, d in G.edges(data=True)]

    nx.draw_networkx_edges(
        G, pos, ax=ax, width=edge_widths, edge_color="#546E7A",
        arrows=True, arrowsize=12, arrowstyle="->",
        connectionstyle="arc3,rad=0.15", alpha=0.7,
    )
    nx.draw_networkx_nodes(
        G, pos, ax=ax, node_color=node_colors, node_size=node_sizes,
        edgecolors="#263238", linewidths=1.5,
    )

    # Labels: subzone + member count
    labels = {n: f"{n}\n({len(G.nodes[n]['members'])})" for n in G.nodes}
    nx.draw_networkx_labels(G, pos, labels, ax=ax, font_size=7, font_weight="bold")

    # Edge weight labels for prominent edges
    prominent_edges = {(u, v): d["weight"] for u, v, d in G.edges(data=True)
                        if d["weight"] >= max_w * 0.3}
    if prominent_edges:
        nx.draw_networkx_edge_labels(
            G, pos, edge_labels=prominent_edges, ax=ax,
            font_size=6, font_color="#B71C1C",
            bbox=dict(boxstyle="round,pad=0.2", fc="white", ec="none", alpha=0.8),
        )

    ax.set_title(f"(a) Zone-Aggregated Graph — {len(G.nodes)} zones, {len(G.edges)} flows",
                 fontsize=11, fontweight="bold")
    ax.axis("off")


# ─── Panel (b): Alert-Focused Graph ────────────────────────────────────────

def panel_alert_focused(ax, graph_data, alerts_data, max_blast=40):
    """Draw only alert sources + their blast radius (from BFS)."""
    if not isinstance(alerts_data, list) or not alerts_data:
        ax.text(0.5, 0.5, "No alerts", ha="center", va="center", fontsize=14)
        ax.axis("off")
        ax.set_title("(b) Alert-Focused Graph", fontsize=11, fontweight="bold")
        return

    # Collect alert sources and their blast radius
    sources = set()
    scan_sources = set()
    blast_targets = defaultdict(set)  # source -> {reached IPs}

    for alert in alerts_data:
        src = alert.get("source", "")
        atype = alert.get("type", "")
        sources.add(src)
        if atype == "port_scan":
            scan_sources.add(src)
        if atype == "blast_radius":
            details = alert.get("details", {})
            if isinstance(details, dict):
                for r in details.get("reachable", []):
                    blast_targets[src].add(r.get("ip", ""))

    # Build focused graph: alert sources + their direct neighbors in blast radius
    G = nx.DiGraph()
    for src in sources:
        G.add_node(src)

    # Add blast targets (capped)
    for src, targets in blast_targets.items():
        for t in list(targets)[:max_blast]:
            if t:
                G.add_node(t)
                G.add_edge(src, t)

    # Also include edges between alert sources from original graph
    all_graph_edges = {(e["from"], e["to"]) for e in graph_data.get("edges", [])}
    for u in list(G.nodes):
        for v in list(G.nodes):
            if u != v and (u, v) in all_graph_edges:
                G.add_edge(u, v)

    if not G.nodes:
        ax.text(0.5, 0.5, "No alert graph", ha="center", va="center", fontsize=14)
        ax.axis("off")
        ax.set_title("(b) Alert-Focused Graph", fontsize=11, fontweight="bold")
        return

    # Layout: sources at center, targets radiating out
    if scan_sources:
        # Use shell layout: sources inner, targets outer
        inner = [n for n in G.nodes if n in sources]
        outer = [n for n in G.nodes if n not in sources]
        pos = nx.shell_layout(G, nlist=[inner, outer] if outer else [inner])
    else:
        pos = nx.spring_layout(G, k=1.5, iterations=50, seed=42)

    # Style nodes
    node_colors, node_sizes, node_edges = [], [], []
    for n in G.nodes:
        zone, _ = classify_ip(n)
        if n in scan_sources:
            node_colors.append("#FFEB3B")
            node_sizes.append(800)
            node_edges.append("#F57F17")
        elif n in sources:
            node_colors.append("#FF8A65")
            node_sizes.append(600)
            node_edges.append("#D84315")
        else:
            node_colors.append(ZONE_COLORS.get(zone, "#BDBDBD"))
            node_sizes.append(200)
            node_edges.append("#455A64")

    nx.draw_networkx_edges(
        G, pos, ax=ax, edge_color="#E57373", width=0.8,
        arrows=True, arrowsize=10, alpha=0.6,
    )
    nx.draw_networkx_nodes(
        G, pos, ax=ax, node_color=node_colors, node_size=node_sizes,
        edgecolors=node_edges, linewidths=1.8,
    )

    # Label only sources (targets too noisy)
    labels = {n: n for n in sources if n in G.nodes}
    nx.draw_networkx_labels(
        G, pos, labels, ax=ax, font_size=7, font_weight="bold",
        bbox=dict(boxstyle="round,pad=0.2", fc="white", ec="#D84315", alpha=0.9),
    )

    ax.set_title(
        f"(b) Alert-Focused Graph — {len(sources)} sources, "
        f"{len(G.nodes) - len(sources)} blast targets",
        fontsize=11, fontweight="bold",
    )
    ax.axis("off")


# ─── Panel (c): Alerts Timeline ────────────────────────────────────────────

def panel_alerts_timeline(ax, alerts_data):
    """Scatter plot: x=time, y=alert type, color=type."""
    if not isinstance(alerts_data, list) or not alerts_data:
        ax.text(0.5, 0.5, "No alerts", ha="center", va="center", fontsize=14)
        ax.set_title("(c) Alerts Timeline", fontsize=11, fontweight="bold")
        return

    types = ["port_scan", "blast_radius", "scc_anomaly"]
    y_map = {t: i for i, t in enumerate(types)}

    xs_by_type = defaultdict(list)
    ys_by_type = defaultdict(list)
    labels_by_type = defaultdict(list)

    # Normalize timestamps to seconds relative to first alert
    if alerts_data:
        t0 = min(a.get("timestamp_ns", 0) for a in alerts_data)
    else:
        t0 = 0

    for alert in alerts_data:
        atype = alert.get("type", "unknown")
        if atype not in y_map:
            continue
        ts = alert.get("timestamp_ns", 0)
        sec = (ts - t0) / 1e9
        xs_by_type[atype].append(sec)
        ys_by_type[atype].append(y_map[atype])
        labels_by_type[atype].append(alert.get("source", ""))

    for atype in types:
        if xs_by_type[atype]:
            ax.scatter(
                xs_by_type[atype], ys_by_type[atype],
                c=ALERT_COLORS.get(atype, "#9E9E9E"),
                s=120, alpha=0.8, edgecolors="black", linewidths=0.8,
                label=atype, zorder=3,
            )

    ax.set_yticks(list(y_map.values()))
    ax.set_yticklabels(list(y_map.keys()), fontsize=9)
    ax.set_xlabel("Time (seconds since first alert)", fontsize=9)
    ax.set_title(f"(c) Alerts Timeline — {len(alerts_data)} events",
                 fontsize=11, fontweight="bold")
    ax.grid(axis="x", linestyle="--", alpha=0.4)
    ax.set_axisbelow(True)
    ax.legend(fontsize=8, loc="upper right", framealpha=0.9)

    # Widen y range for breathing room
    ax.set_ylim(-0.5, len(types) - 0.5)


# ─── Panel (d): Top Talkers Bar Chart ──────────────────────────────────────

def panel_top_talkers(ax, graph_data, alerts_data, top_n=10):
    """Horizontal bar chart: top IPs by out-degree."""
    # Count out-edges per source
    out_degree = Counter()
    for e in graph_data.get("edges", []):
        out_degree[e["from"]] += 1

    if not out_degree:
        ax.text(0.5, 0.5, "No edges", ha="center", va="center", fontsize=14)
        ax.set_title("(d) Top Talkers", fontsize=11, fontweight="bold")
        return

    top = out_degree.most_common(top_n)
    ips = [ip for ip, _ in top]
    counts = [c for _, c in top]

    # Flag alert sources
    alert_sources = set()
    if isinstance(alerts_data, list):
        for a in alerts_data:
            alert_sources.add(a.get("source", ""))

    # Color bars: red if alert source, zone color otherwise
    colors = []
    for ip in ips:
        if ip in alert_sources:
            colors.append("#D32F2F")
        else:
            zone, _ = classify_ip(ip)
            colors.append(ZONE_COLORS.get(zone, "#BDBDBD"))

    y_pos = range(len(ips))
    bars = ax.barh(y_pos, counts, color=colors, edgecolor="black", linewidth=0.8)

    # Value labels
    for bar, count in zip(bars, counts):
        ax.text(
            bar.get_width() + max(counts) * 0.01,
            bar.get_y() + bar.get_height() / 2,
            f"{count}",
            va="center", fontsize=8, fontweight="bold",
        )

    ax.set_yticks(list(y_pos))
    ax.set_yticklabels(ips, fontsize=8, family="monospace")
    ax.invert_yaxis()
    ax.set_xlabel("Out-degree (unique destinations)", fontsize=9)
    ax.set_title(f"(d) Top {len(ips)} Talkers — red = alerted",
                 fontsize=11, fontweight="bold")
    ax.grid(axis="x", linestyle="--", alpha=0.4)
    ax.set_axisbelow(True)


# ─── Dashboard composition ─────────────────────────────────────────────────

def render_dashboard(graph_data, alerts_data, stats_data, output_path):
    fig = plt.figure(figsize=(20, 14), facecolor="#FAFAFA")
    gs = GridSpec(
        2, 2, figure=fig,
        hspace=0.28, wspace=0.15,
        left=0.04, right=0.98, top=0.92, bottom=0.06,
    )

    ax_a = fig.add_subplot(gs[0, 0])
    ax_b = fig.add_subplot(gs[0, 1])
    ax_c = fig.add_subplot(gs[1, 0])
    ax_d = fig.add_subplot(gs[1, 1])

    for ax in (ax_a, ax_b, ax_c, ax_d):
        ax.set_facecolor("#FAFAFA")

    panel_zone_aggregated(ax_a, graph_data)
    panel_alert_focused(ax_b, graph_data, alerts_data)
    panel_alerts_timeline(ax_c, alerts_data)
    panel_top_talkers(ax_d, graph_data, alerts_data)

    # Main title
    stats = stats_data if isinstance(stats_data, dict) else {}
    title = "Zero-Trust Network Mapper — Dashboard"
    subtitle = (
        f"{stats.get('total_events', '?')} events | "
        f"{stats.get('graph_vertices', '?')} vertices | "
        f"{stats.get('graph_edges', '?')} edges | "
        f"{stats.get('alerts_count', '?')} alerts | "
        f"{stats.get('nontrivial_sccs', '?')} SCCs"
    )
    algos = stats.get("algorithms", {})
    subtitle += (
        f"\nAlgorithms: {algos.get('frequency', '?')} / "
        f"{algos.get('scc', '?')} / {algos.get('reachability', '?')}"
    )

    fig.suptitle(f"{title}\n{subtitle}", fontsize=14, fontweight="bold", y=0.985)

    # Global legend — zone colors (bottom)
    zone_patches = [
        mpatches.Patch(color=c, label=z.capitalize())
        for z, c in ZONE_COLORS.items()
        if z not in ("loopback", "unknown")
    ]
    fig.legend(
        handles=zone_patches,
        loc="lower center", ncol=len(zone_patches),
        fontsize=9, frameon=False,
        bbox_to_anchor=(0.5, 0.01),
    )

    plt.savefig(output_path, dpi=140, bbox_inches="tight",
                facecolor=fig.get_facecolor())
    plt.close(fig)
    print(f"Dashboard rendered to: {output_path}", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(
        description="Zero-Trust Network Mapper — 4-Panel Dashboard"
    )
    parser.add_argument("--graph",  default="output/graph.json")
    parser.add_argument("--alerts", default="output/alerts.json")
    parser.add_argument("--stats",  default="output/stats.json")
    parser.add_argument("--output", default="output/dashboard.png")
    args = parser.parse_args()

    graph_data = load_json(args.graph)
    alerts_data = load_json(args.alerts)
    stats_data = load_json(args.stats)

    if not graph_data:
        print("Error: No graph data. Run the pipeline first.", file=sys.stderr)
        sys.exit(1)

    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    render_dashboard(graph_data, alerts_data, stats_data, args.output)


if __name__ == "__main__":
    main()
