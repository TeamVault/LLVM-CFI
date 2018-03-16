import csv
import sys
import numpy as np

input_path = "analysis/"

name = "lighttpd"
noVirtual = True


def printWarnings():
    lines = []
    with open(sys.argv[1]) as f:
        f.readline()
        prev_line = ""
        for i, line in enumerate(f.readlines()):
            if line.startswith("SD WARNING]"):
                lines.append(prev_line)
                lines.append(line)
                prev_line = ""
            else:
                prev_line = line

    for line in lines:
        print(line, end="")


class CallSiteInfo(object):
    def __init__(self,
                 dwarf,
                 params,
                 baseline,
                 srcType,
                 safeSrcType,
                 binType,
                 baseline_vtarget,
                 srcType_vtarget,
                 safeSrcType_vtarget,
                 binType_vtarget
                 ):
        self.dwarf = dwarf
        self.params = int(params)
        self.baseline = int(baseline)
        self.baseline_vtarget = int(baseline_vtarget)

        self.srcType = int(srcType)
        self.safeSrcType = int(safeSrcType)
        self.binType = int(binType)

        self.srcType_vtarget = int(srcType_vtarget)
        self.safeSrcType_vtarget = int(safeSrcType_vtarget)
        self.binType_vtarget = int(binType_vtarget)


class IndirectCallSiteInfo(CallSiteInfo):
    def __init__(self,
                 dwarf,
                 params,
                 baseline,
                 srcType,
                 safeSrcType,
                 binType,
                 baseline_vtarget,
                 srcType_vtarget,
                 safeSrcType_vtarget,
                 binType_vtarget
                 ):
        super().__init__(
            dwarf,
            params,
            baseline,
            srcType,
            safeSrcType,
            binType,
            baseline_vtarget,
            srcType_vtarget,
            safeSrcType_vtarget,
            binType_vtarget
        )


class VirtualCallSiteInfo(CallSiteInfo):
    def __init__(self,
                 dwarf,
                 params,
                 baseline,
                 preciseSrcType,
                 srcType,
                 safeSrcType,
                 binType,
                 baseline_vtarget,
                 preciseSrcType_vtarget,
                 srcType_vtarget,
                 safeSrcType_vtarget,
                 binType_vtarget,
                 vTableSubHierarchy,
                 classSubHierarchy,
                 classIsland,
                 allVTables
                 ):
        super().__init__(
            dwarf,
            params,
            baseline,
            srcType,
            safeSrcType,
            binType,
            baseline_vtarget,
            srcType_vtarget,
            safeSrcType_vtarget,
            binType_vtarget
        )

        self.preciseSrcType = int(preciseSrcType)
        self.preciseSrcType_vtarget = int(preciseSrcType_vtarget)
        self.vTableSubHierarchy = int(vTableSubHierarchy)
        self.classSubHierarchy = int(classSubHierarchy)
        self.classIsland = int(classIsland)
        self.allVTables = int(allVTables)


def parse_virtual(file):
    reader = csv.reader(file)

    callsites = []
    index = 0
    for row in reader:
        if index == 0 or index == 1:
            index = index + 1
            continue

        if len(row) < 22:
            raise ValueError("Row " + index + " is " + str(len(row)) + " long (should be 22)!")

        callsite = VirtualCallSiteInfo(
            dwarf=row[0],
            params=row[4],
            baseline=row[10],
            preciseSrcType=row[6],
            srcType=row[7],
            safeSrcType=row[8],
            binType=row[9],
            baseline_vtarget=row[16],
            preciseSrcType_vtarget=row[12],
            srcType_vtarget=row[13],
            safeSrcType_vtarget=row[14],
            binType_vtarget=row[15],
            vTableSubHierarchy=row[18],
            classSubHierarchy=row[19],
            classIsland=row[20],
            allVTables=row[21]
        )

        callsites.append(callsite)
        index = index + 1

    return callsites


def parse_indirect(file):
    reader = csv.reader(file)

    callsites = []

    index = 0
    for row in reader:
        if index == 0 or index == 1:
            index = index + 1
            continue

        if len(row) < 17:
            raise ValueError("Row " + index + " is " + str(len(row)) + " long (should be 17)!")

        callsite = IndirectCallSiteInfo(
            dwarf=row[0],
            params=row[4],
            baseline=row[10],
            srcType=row[7],
            safeSrcType=row[8],
            binType=row[9],
            baseline_vtarget=row[16],
            srcType_vtarget=row[13],
            safeSrcType_vtarget=row[14],
            binType_vtarget=row[15]
        )

        callsites.append(callsite)
        index = index + 1

    return callsites


def geo_mean_overflow(iterable):
    a = np.log(iterable)
    return np.exp(a.sum() / len(a))


def statistics(virtual_callsites, indirect_callsites):
    all_callsites = virtual_callsites + indirect_callsites
    a_a_data = statistics_all_all(all_callsites)
    # statistics_all_vtarget(all_callsites)
    if len(virtual_callsites) > 0:
        v_a_data = statistics_vcall_all(virtual_callsites)
        v_v_data = statistics_vcall_vtarget(virtual_callsites)

    # print table 4
    if len(virtual_callsites) > 0:
        print()
        print("TABLE 4")
        print()
        min_str = "\t&min\t&\t&" + str(all_callsites[0].baseline) + "\t&" + \
                  str(all_callsites[0].baseline_vtarget) + "\t&" + \
                  v_a_data[0].min + " (" + v_v_data[0].min + ")\t&" + \
                  v_a_data[1].min + " (" + v_v_data[1].min + ")\t&" + \
                  v_a_data[2].min + " (" + v_v_data[2].min + ")\t&" + \
                  v_a_data[3].min + " (" + v_v_data[3].min + ")\t&" + \
                  v_v_data[4].min + "\t&" + \
                  v_v_data[5].min + "\t&" + \
                  v_v_data[6].min + "\t&" + \
                  v_v_data[7].min + "\t\\\\"

        percent_str = "\t&90p\t&\t&" + str(all_callsites[0].baseline) + "\t&" + \
                  str(all_callsites[0].baseline_vtarget) + "\t&" + \
                  v_a_data[0].percentile + " (" + v_v_data[0].percentile + ")\t&" + \
                  v_a_data[1].percentile + " (" + v_v_data[1].percentile + ")\t&" + \
                  v_a_data[2].percentile + " (" + v_v_data[2].percentile + ")\t&" + \
                  v_a_data[3].percentile + " (" + v_v_data[3].percentile + ")\t&" + \
                  v_v_data[4].percentile + "\t&" + \
                  v_v_data[5].percentile + "\t&" + \
                  v_v_data[6].percentile + "\t&" + \
                  v_v_data[7].percentile + "\t\\\\"

        max_str = "\t&max\t&none\t&" + str(all_callsites[0].baseline) + "\t&" + \
                  str(all_callsites[0].baseline_vtarget) + "\t&" + \
                  v_a_data[0].max + " (" + v_v_data[0].max + ")\t&" + \
                  v_a_data[1].max + " (" + v_v_data[1].max + ")\t&" + \
                  v_a_data[2].max + " (" + v_v_data[2].max + ")\t&" + \
                  v_a_data[3].max + " (" + v_v_data[3].max + ")\t&" + \
                  v_v_data[4].max + "\t&" + \
                  v_v_data[5].max + "\t&" + \
                  v_v_data[6].max + "\t&" + \
                  v_v_data[7].max + "\t\\\\"

        median_str = "\t&median\t&\t&" + str(all_callsites[0].baseline) + "\t&" + \
                  str(all_callsites[0].baseline_vtarget) + "\t&" + \
                  v_a_data[0].median + " (" + v_v_data[0].median + ")\t&" + \
                  v_a_data[1].median + " (" + v_v_data[1].median + ")\t&" + \
                  v_a_data[2].median + " (" + v_v_data[2].median + ")\t&" + \
                  v_a_data[3].median + " (" + v_v_data[3].median + ")\t&" + \
                  v_v_data[4].median + "\t&" + \
                  v_v_data[5].median + "\t&" + \
                  v_v_data[6].median + "\t&" + \
                  v_v_data[7].median + "\t\\\\"

        mean_str = "\t&mean\t&\t&" + str(all_callsites[0].baseline) + "\t&" + \
                  str(all_callsites[0].baseline_vtarget) + "\t&" + \
                  v_a_data[0].mean + " (" + v_v_data[0].mean + ")\t&" + \
                  v_a_data[1].mean + " (" + v_v_data[1].mean + ")\t&" + \
                  v_a_data[2].mean + " (" + v_v_data[2].mean + ")\t&" + \
                  v_a_data[3].mean + " (" + v_v_data[3].mean + ")\t&" + \
                  v_v_data[4].mean + "\t&" + \
                  v_v_data[5].mean + "\t&" + \
                  v_v_data[6].mean + "\t&" + \
                  v_v_data[7].mean + "\t\\\\"

        print(min_str)
        print(percent_str)
        print(max_str)
        print(median_str)
        print(mean_str)

    # print table 5
    print()
    print("TABLE 5")
    print()

    min_str = "\t&min\t&\t&" + str(all_callsites[0].baseline) + "\t&&" + \
              a_a_data[0].min + "\t&" + \
              a_a_data[1].min + "\t&" + \
              a_a_data[2].min + "\t\\\\"

    percent_str = "\t&90p\t&\t&" + str(all_callsites[0].baseline) + "\t&&" + \
              a_a_data[0].percentile + "\t&" + \
              a_a_data[1].percentile + "\t&" + \
              a_a_data[2].percentile + "\t\\\\"

    max_str = "\t&max\t&\t&" + str(all_callsites[0].baseline) + "\t&&" + \
              a_a_data[0].max + "\t&" + \
              a_a_data[1].max + "\t&" + \
              a_a_data[2].max + "\t\\\\"

    median_str = "\t&median\t&\t&" + str(all_callsites[0].baseline) + "\t&&" + \
              a_a_data[0].median + "\t&" + \
              a_a_data[1].median + "\t&" + \
              a_a_data[2].median + "\t\\\\"

    mean_str = "\t&mean\t&\t&" + str(all_callsites[0].baseline) + "\t&&" + \
              a_a_data[0].mean + "\t&" + \
              a_a_data[1].mean + "\t&" + \
              a_a_data[2].mean + "\t\\\\"

    print(min_str)
    print(percent_str)
    print(max_str)
    print(median_str)
    print(mean_str)

    # print table 7
    print()
    print("TABLE 7")
    print()

    table7_str = "\t&" + str(all_callsites[0].baseline) + "\t&" + \
              a_a_data[0].norm_mean + "\t&" + a_a_data[0].norm_sd + "\t&" + a_a_data[0].norm_percent + "\t&" + \
              a_a_data[1].norm_mean + "\t&" + a_a_data[1].norm_sd + "\t&" + a_a_data[1].norm_percent + "\t&" + \
              a_a_data[2].norm_mean + "\t&" + a_a_data[2].norm_sd + "\t&" + a_a_data[2].norm_percent + "\t\\\\"

    print(table7_str)

    # print table 6
    if len(virtual_callsites) > 0:
        print()
        print("TABLE 6")
        print()

        table6_str = "\t&" + str(virtual_callsites[0].baseline_vtarget) + "\t&" + \
                     v_a_data[0].norm_mean + "\t&" + v_a_data[0].norm_sd + "\t&" + v_a_data[0].norm_percent + "\t&" + \
                     v_a_data[1].norm_mean + "\t&" + v_a_data[1].norm_sd + "\t&" + v_a_data[1].norm_percent + "\t&" + \
                     v_a_data[2].norm_mean + "\t&" + v_a_data[2].norm_sd + "\t&" + v_a_data[2].norm_percent + "\t&" + \
                     v_a_data[3].norm_mean + "\t&" + v_a_data[3].norm_sd + "\t&" + v_a_data[3].norm_percent + "\t&" + \
                     v_v_data[4].norm_mean + "\t&" + v_v_data[4].norm_sd + "\t&" + v_v_data[4].norm_percent + "\t&" + \
                     v_v_data[5].norm_mean + "\t&" + v_v_data[5].norm_sd + "\t&" + v_v_data[5].norm_percent + "\t&" + \
                     v_v_data[6].norm_mean + "\t&" + v_v_data[6].norm_sd + "\t&" + v_v_data[6].norm_percent + "\t&" + \
                     v_v_data[7].norm_mean + "\t&" + v_v_data[7].norm_sd + "\t&" + v_v_data[7].norm_percent + "\t\\\\"

        print(table6_str)
        print()



class Table(object):
    def __init__(self):
        self.mean = ""
        self.median = ""
        self.min = ""
        self.max = ""
        self.percentile = ""
        self.sd = ""
        self.norm_mean = ""
        self.norm_sd = ""
        self.norm_percent = ""


def statistics_all_all(all_callsites):
    table_srcType = Table()
    table_safeSrcType = Table()
    table_binType = Table()


    if len(all_callsites) > 0:
        baseline = all_callsites[0].baseline
        print()
        print("All callsites -> all targets:")
        print("Number of callsites: " + str(len(all_callsites)))
        print("Baseline: " + str(baseline))
        print()

        srcType_list = [callsite.srcType for callsite in all_callsites]
        safeSrcType_list = [callsite.safeSrcType for callsite in all_callsites]
        binType_list = [callsite.binType for callsite in all_callsites]

        printStatistics(srcType_list, "srcType", baseline=baseline, table=table_srcType)
        printStatistics(safeSrcType_list, "safeSrcType", baseline=baseline, table=table_safeSrcType)
        printStatistics(binType_list, "binType", baseline=baseline, table=table_binType)

        return [table_binType, table_safeSrcType, table_srcType]

    return []


def statistics_all_vtarget(all_callsites):
    if len(all_callsites) > 0:
        baseline = all_callsites[0].baseline_vtarget
        print()
        print("All callsites -> virtual targets only:")
        print("Number of callsites: " + str(len(all_callsites)))
        print("Baseline: " + str(baseline))
        print()

        srcType_vtarget_list = [callsite.srcType_vtarget for callsite in all_callsites]
        safeSrcType_vtarget_list = [callsite.safeSrcType_vtarget for callsite in all_callsites]
        binType_vtarget_list = [callsite.binType_vtarget for callsite in all_callsites]

        printStatistics(srcType_vtarget_list, "srcType-vtarget", baseline=baseline)
        printStatistics(safeSrcType_vtarget_list, "safeSrcType-vtarget", baseline=baseline)
        printStatistics(binType_vtarget_list, "binType-vtarget", baseline=baseline)


def statistics_vcall_all(virtual_callsites):
    table_preciseSrcType = Table()
    table_srcType = Table()
    table_safeSrcType = Table()
    table_binType = Table()

    if len(virtual_callsites) > 0:
        baseline = virtual_callsites[0].baseline
        print()
        print("Virtual callsites -> all targets:")
        print("Number of callsites: " + str(len(virtual_callsites)))
        print("Baseline: " + str(baseline))
        print()

        preciseSrcType_list = [callsite.preciseSrcType for callsite in virtual_callsites]
        srcType_list = [callsite.srcType for callsite in virtual_callsites]
        safeSrcType_list = [callsite.safeSrcType for callsite in virtual_callsites]
        binType_list = [callsite.binType for callsite in virtual_callsites]

        printStatistics(preciseSrcType_list, "preciseSrcType", table=table_preciseSrcType, baseline=baseline)
        printStatistics(srcType_list, "srcType", table=table_srcType, baseline=baseline)
        printStatistics(safeSrcType_list, "safeSrcType", table=table_safeSrcType, baseline=baseline)
        printStatistics(binType_list, "binType", table=table_binType, baseline=baseline)

        return [table_binType, table_safeSrcType, table_srcType, table_preciseSrcType]

    return []


def statistics_vcall_vtarget(virtual_callsites):
    table_preciseSrcType = Table()
    table_srcType = Table()
    table_safeSrcType = Table()
    table_binType = Table()
    table_vTableSubHierarchy = Table()
    table_classSubHierarchy = Table()
    table_classIsland = Table()
    table_allVTables = Table()

    if len(virtual_callsites) > 0:
        baseline = virtual_callsites[0].baseline_vtarget
        print()
        print("Virtual callsites -> virtual targets:")
        print("Number of callsites: " + str(len(virtual_callsites)))
        print("Baseline: " + str(baseline))
        print()

        preciseSrcType_vtarget_list = [callsite.preciseSrcType_vtarget for callsite in virtual_callsites]
        srcType_vtarget_list = [callsite.srcType_vtarget for callsite in virtual_callsites]
        safeSrcType_vtarget_list = [callsite.safeSrcType_vtarget for callsite in virtual_callsites]
        binType_vtarget_list = [callsite.binType_vtarget for callsite in virtual_callsites]

        printStatistics(preciseSrcType_vtarget_list, "preciseSrcType-vtarget", table=table_preciseSrcType, baseline=baseline)
        printStatistics(srcType_vtarget_list, "srcType-vtarget", table=table_srcType, baseline=baseline)
        printStatistics(safeSrcType_vtarget_list, "safeSrcType-vtarget", table=table_safeSrcType, baseline=baseline)
        printStatistics(binType_vtarget_list, "binType-vtarget", table=table_binType, baseline=baseline)

        vTableSubHierarchy_list = [callsite.vTableSubHierarchy for callsite in virtual_callsites]
        classSubHierarchy_list = [callsite.classSubHierarchy for callsite in virtual_callsites]
        classIsland_list = [callsite.classIsland for callsite in virtual_callsites]
        allVTables_list = [callsite.allVTables for callsite in virtual_callsites]

        printStatistics(vTableSubHierarchy_list, "vTableSubHierarchy", table=table_vTableSubHierarchy, baseline=baseline)
        printStatistics(classSubHierarchy_list, "classSubHierarchy", table=table_classSubHierarchy, baseline=baseline)
        printStatistics(classIsland_list, "classIsland", table=table_classIsland, baseline=baseline)
        printStatistics(allVTables_list, "allVTables", table=table_allVTables, baseline=baseline)

        return [table_binType, table_safeSrcType, table_srcType, table_preciseSrcType,
                table_allVTables, table_classIsland, table_classSubHierarchy, table_vTableSubHierarchy]

    return []


def printStatistics(data_list, title, baseline, table=None):
    arr = np.asarray(data_list)

    mean = str(round(arr.mean(), 2))
    median = str(round(np.median(arr), 2))
    percentile = str(round(np.percentile(arr, 90), 2))
    sd = str(round(np.std(arr, ddof=0), 2))
    min = str(round(arr.min(), 2))
    max = str(round(arr.max(), 2))

    norm_mean = str(round(arr.mean() / float(baseline) * 100, 2))
    norm_sd = str(round(np.std(arr, ddof=0) / float(baseline) * 100, 2))
    norm_percent = str(round(np.percentile(arr, 90) / float(baseline) * 100, 2))


    print(title)
    print("Mean: " + mean)
    print("SD: " + sd)
    print("Median: " + median)
    print("90p: " + percentile)
    print("Min: " + min)
    print("Max: " + max)
    print("Norm-mean: " + norm_mean)
    print("Norm-SD: " + norm_sd)
    print("Norm-90p: " + norm_percent)

    print()

    if table:
        table.mean = mean
        table.min = min
        table.max = max
        table.median = median
        table.percentile = percentile
        table.sd = sd
        table.norm_mean = norm_mean
        table.norm_sd = norm_sd
        table.norm_percent = norm_percent



def main():
    print("Start virtual parsing...")
    if not noVirtual:
        virtual_stream = open(input_path + name + "-Virtual.csv", "r")
        virtual_callsites = parse_virtual(virtual_stream)
    else:
        virtual_callsites = []

    print("Start indirect parsing...")
    indirect_stream = open(input_path + name + "-Indirect.csv", "r")
    indirect_callsites = parse_indirect(indirect_stream)

    statistics(virtual_callsites, indirect_callsites)


if __name__ == '__main__':
    main()
