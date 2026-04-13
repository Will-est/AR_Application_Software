import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import argparse
from pathlib import Path

def generate_performance_graphs(csv_path):
    # 1. Setup paths and directories
    csv_file = Path(csv_path)
    if not csv_file.exists():
        print(f"Error: File {csv_path} not found.")
        return

    # UPDATED: Folder name is now data_graphs_[csv_filename]
    output_folder_name = f"data_graphs_{csv_file.stem}"
    output_dir = csv_file.parent / output_folder_name
    
    output_dir.mkdir(parents=True, exist_ok=True)
    print(f"Outputting graphs to folder: {output_dir}")

    # 2. Load and prep data
    df = pd.read_csv(csv_file)
    
    # Ensure time is relative to start
    df['time_sec'] = (df['mono_ms'] - df['mono_ms'].iloc[0]) / 1000.0
    sns.set_theme(style="whitegrid")

    # Define the metrics for individual plots
    metrics = [
        ('fps', 'Throughput: FPS', 'FPS', '01_fps.png', 'line'),
        ('cpu_pct', 'CPU Utilization', 'CPU %', '02_cpu.png', 'line'),
        (['vmrss_kb', 'vmsize_kb'], 'Memory Usage', 'KB', '03_memory.png', 'memory'),
        ('threads', 'Thread Count', 'Count', '04_threads.png', 'step'),
        (['capture_dt_avg_ms', 'capture_dt_max_ms'], 'Frame Arrival Delta', 'ms', '05_arrival_delta.png', 'range'),
        (['frame_proc_avg_ms', 'frame_proc_max_ms'], 'Total processFrame Time', 'ms', '06_process_time.png', 'range'),
        (['preproc_avg_ms', 'preproc_max_ms'], 'Preprocess Time', 'ms', '07_preproc_time.png', 'range'),
        (['arrival_to_preproc_end_avg_ms', 'arrival_to_preproc_end_max_ms'], 'Arrival to Preproc Latency', 'ms', '08_latency.png', 'range'),
        ('patterns_found_per_s', 'Patterns Found Per Second', 'Count', '09_patterns.png', 'bar'),
        ('frames_total', 'Cumulative Frames', 'Total', '10_total_frames.png', 'line')
    ]

    # 3. Generate individual pictures
    for cols, title, ylabel, fname, ptype in metrics:
        plt.figure(figsize=(10, 6))
        
        if ptype == 'line':
            plt.plot(df['time_sec'], df[cols], marker='.')
        elif ptype == 'step':
            plt.step(df['time_sec'], df[cols], where='post')
        elif ptype == 'bar':
            plt.bar(df['time_sec'], df[cols], width=0.8, alpha=0.7)
        elif ptype == 'memory':
            plt.plot(df['time_sec'], df['vmrss_kb']/1024, label='Physical (MB)')
            plt.plot(df['time_sec'], df['vmsize_kb']/1024, label='Virtual (MB)', linestyle='--')
            plt.legend()
            ylabel = 'MB'
        elif ptype == 'range':
            plt.plot(df['time_sec'], df[cols[0]], label='Avg', marker='.')
            plt.plot(df['time_sec'], df[cols[1]], label='Max', alpha=0.3)
            plt.legend()

        plt.title(title)
        plt.xlabel('Time (s)')
        plt.ylabel(ylabel)
        plt.tight_layout()
        plt.savefig(output_dir / fname)
        plt.close()

    # 4. Generate the main dashboard
    fig, axes = plt.subplots(5, 2, figsize=(16, 24))
    fig.suptitle(f'Performance Analysis: {csv_file.name}', fontsize=20)
    axes = axes.flatten()

    axes[0].plot(df['time_sec'], df['fps'], marker='.', color='tab:blue')
    axes[0].set_title('Throughput: FPS')

    axes[1].plot(df['time_sec'], df['cpu_pct'], marker='.', color='tab:red')
    axes[1].set_title('CPU Utilization (%)')

    axes[2].plot(df['time_sec'], df['vmrss_kb'] / 1024, label='RSS', color='tab:green')
    axes[2].plot(df['time_sec'], df['vmsize_kb'] / 1024, label='Size', color='tab:olive', ls='--')
    axes[2].set_title('Memory (MB)')

    axes[3].step(df['time_sec'], df['threads'], where='post', color='tab:purple')
    axes[3].set_title('Threads')

    axes[4].plot(df['time_sec'], df['capture_dt_avg_ms'], label='Avg')
    axes[4].fill_between(df['time_sec'], df['capture_dt_avg_ms'], df['capture_dt_max_ms'], alpha=0.2)
    axes[4].set_title('Arrival Delta (ms)')

    axes[5].plot(df['time_sec'], df['frame_proc_avg_ms'], color='tab:orange')
    axes[5].set_title('Total processFrame Time (ms)')

    axes[6].plot(df['time_sec'], df['preproc_avg_ms'], color='tab:cyan')
    axes[6].set_title('Preprocess Time (ms)')

    axes[7].plot(df['time_sec'], df['arrival_to_preproc_end_avg_ms'], color='tab:brown')
    axes[7].set_title('Arrival -> Preproc Latency (ms)')

    axes[8].bar(df['time_sec'], df['patterns_found_per_s'], color='gold')
    axes[8].set_title('Patterns Found / s')

    axes[9].plot(df['time_sec'], df['frames_total'], color='black')
    axes[9].set_title('Cumulative Frames')

    for ax in axes:
        ax.set_xlabel('Seconds')

    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    plt.savefig(output_dir / '00_full_dashboard.png')
    plt.close()
    print(f"Processing complete. Images saved in '{output_dir.name}'")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Turn CSV metrics into graphs.')
    parser.add_argument('csv_path', type=str, help='Path to the input .csv file')
    
    args = parser.parse_args()
    generate_performance_graphs(args.csv_path)