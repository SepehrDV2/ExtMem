import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt
#import scienceplots

# Set Seaborn style
sns.set(style="whitegrid")
#plt.style.use(['science',  'seaborn-colorblind'])

# "extmem-workingset-write-8-threads.log"

# List of system names
system_names = ['cgroup', 'extmem']

# List to store throughput data for each system
throughput_data = []

# Loop through each system
for system_name in system_names:
    # Read the CSV file
    file_path = f"{system_name}-workingset-write-8-threads.log"
    df = pd.read_csv(file_path, header=0, names=['device', 'col1', 'col2', 'col3', 'timestamp', 'throughput', 'col6', 'col7', 'col8', 'col9'])
    df['throughput'] = df['throughput'] * (1024*1024*1024) / (4096*1000*1000)
    # Store DataFrame with throughput and timestamp data for each system
    throughput_data.append(df[['timestamp', 'throughput']])

# Plot throughput over time for all systems using Seaborn
plt.figure(figsize=(10, 6))
for i, df in enumerate(throughput_data):
    sns.lineplot(data=df, x='timestamp', y='throughput', label=system_names[i], marker='o', markersize=8)

# Add labels and title with readable font sizes
plt.xlabel('Time (seconds)', fontsize=16)
plt.ylabel('Throughput (Million updates/sec)', fontsize=16)
plt.title('Throughput Over Time for Different Systems', fontsize=16)

plt.yscale("log", base=2)
# Set font sizes for tick labels
plt.xticks(fontsize=16)
#plt.yticks([1,2,4,8,16], [1,2,4,8,16],fontsize=12)
plt.yticks(fontsize=16)

# Add legend
plt.legend(fontsize=16)

# Show the plot
plt.grid(True)
plt.tight_layout()

plt.savefig('workingset-time.pdf')

plt.show()
