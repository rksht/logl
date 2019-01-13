# The lengths I go to to test this

import random
import time
import sys

# Run for this many seconds
run_for = 20.0

file_list = ["/tmp/file1", "/tmp/file2", "/tmp/file3", "/tmp/file4"]
file_handles = [open(f, 'w') for f in file_list]
file_count = len(file_handles)

last_ts = time.perf_counter()

time_running = 0.0

def random_string():
    return 'I hate my {}% of my life\n'.format(random.randint(0, 100))


# Randomly choose the number of files to edit
num_files_to_edit = random.randint(1, file_count)

# Then choose the files randomly
files_to_edit = random.sample(range(0, file_count), num_files_to_edit)

# Choose times to edit each file
times_to_edit = [0 for i in range(file_count)]

for i in range(0, file_count):
    if i not in files_to_edit:
        times_to_edit[i] = 0
    else:
        times_to_edit[i] = random.randint(1, 4)


print('Total edits: {}'.format(sum(times_to_edit)))

still_to_edit = set(files_to_edit)

while len(still_to_edit) != 0:
    # Choose a file to edit randomly
    to_edit = random.sample(still_to_edit, 1)[0]

    # Edit this file
    print('Editing file: ', file_list[to_edit])
    file_handles[to_edit].write(random_string())
    times_to_edit[to_edit] -= 1

    print(times_to_edit, still_to_edit)

    # Edit count is 0, we don't edit it anymore
    if times_to_edit[to_edit] == 0:
        still_to_edit.remove(to_edit)

    # Sleep for a while before next edit or don't
    sleep_seconds = random.uniform(0.0, 0.9)
    print('Waiting for: %.1f milliseconds' %(sleep_seconds * 1000))
    sys.stdout.flush()
    time.sleep(sleep_seconds)


# Close all the file handles
for h in file_handles:
    h.close()
