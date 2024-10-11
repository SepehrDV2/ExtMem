import os
import re
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns

def calculate_average_throughput(file_path):
    # Read CSV file, skip first two rows (headers)
    df = pd.read_csv(file_path, skiprows=2)
    # Calculate average of throughput column (7th column)
    avg_throughput = df.iloc[:, 5].mean()
    return avg_throughput

def read_throughput_files(directory):
    throughputs = {}
    file_pattern = re.compile(r'^(?P<system_name>[\w]+)-rand-write-(?P<threads>\d+)-threads\.log$')
    for file_name in os.listdir(directory):
        match = file_pattern.match(file_name)
        if match:
            system_name = match.group('system_name')
            threads = int(match.group('threads'))
            avg_throughput = calculate_average_throughput(os.path.join(directory, file_name))
            if system_name not in throughputs:
                throughputs[system_name] = {}
            throughputs[system_name][threads] = avg_throughput
    return throughputs

def plot_throughput_comparison(throughputs, output_file):
    systems = list(throughputs.keys())
    threads = sorted(set(thread for system in throughputs.values() for thread in system.keys()))
    avg_throughputs_ExtMem = [throughputs['extmem'].get(thread, 0) for thread in threads]
    avg_throughputs_Linux = [throughputs['cgroup'].get(thread, 0) for thread in threads]

    # Plotting
    plt.figure(figsize=(10, 6))
    sns.set_style("whitegrid")
    bar_width = 0.35
    index = range(len(threads))
    plt.bar(index, avg_throughputs_ExtMem, bar_width, label='ExtMem')
    plt.bar([i + bar_width for i in index], avg_throughputs_Linux, bar_width, label='Linux')
    plt.xlabel('Number of Threads')
    plt.ylabel('Throughput')
    plt.title('Average Throughput Comparison')
    plt.xticks([i + bar_width / 2 for i in index], threads)
    plt.legend()
    plt.tight_layout()
    plt.savefig(output_file)
    plt.close()

if __name__ == "__main__":
    directory = "."
    output_file = "random_microbenchmark_throughput.pdf"
    throughputs = read_throughput_files(directory)
    plot_throughput_comparison(throughputs, output_file)
