#!/usr/bin/env Rscript

library(ggplot2)
#library(dplyr)
library(gridExtra)

d = read.csv("out-0.csv")
d$X = NULL
s = read.csv("stats-0.csv")

# facetted tabe
f = data.frame(time=double(), value=double(), type=factor(), subtype=factor(), stringsAsFactors=FALSE)
f = rbind(f, data.frame(time = d$start_time[1:(nrow(d)-1)], value = d$duration[1:(nrow(d)-1)], type="Duration", subtype="Duration"))
f = rbind(f, data.frame(time = d$start_time[1:(nrow(d)-1)], value = log10(d$duration[1:(nrow(d)-1)]), type="Dur. log", subtype="Duration log(10)"))

# special case: blockdev_in_flight
filter = colnames(s) %in% c("blockdev_in_flight", "meminfo_Buffers.", "meminfo_Cached.", "meminfo_Dirty.", "meminfo_Writeback.")
for (x in 2:ncol(s)){
  #name = colnames(s)[x]
  name = c("D", "self",  "self",  "syscalls",       "syscalls", "self",  "self",  "self",                   rep("memory", 4), "dev ios", "dev ios", "dev sectors", "dev ticks", "dev ios", "dev ios", "dev sectors", "dev ticks", "dev in flight", "dev ticks", "queue")[x]
  subt = c("D", "read", "write", "read", "write", "read", "write", "write cancelled", "buffer", "cached", "dirty", "writeback", "read", "merges", "read", "read", "write", "merges", "write", "write", "flight", "ticks", "queue time" )[x]
  if(filter[x]){
    f = rbind(f, data.frame(time = s$start_time, value = s[, x], type=name, subtype=subt))
  }else{
    f = rbind(f, data.frame(time = s$start_time[2:nrow(s)], value = s[2:nrow(s), x] - s[1:(nrow(s)-1), x], type=name, subtype=subt))
  }
}

mn = min(f$time)
ms = max(d$start_time)
f$time = f$time - mn
# add end time marker
f = rbind(f, data.frame(time = d$start_time[nrow(d)] + d$duration[nrow(d)] - mn, value = min(d$duration), type="Duration", subtype="end_time"))

logs = c("Duration")
# p1 = ggplot(subset(f, type %in% logs),  aes(time, value, col=subtype)) + geom_point() + facet_grid(type ~ ., scales="free_y") + # coord_trans(y= "log10")
# p2 = ggplot(subset(f, ! type %in% logs),  aes(time, value, col=subtype)) + geom_point() + facet_grid(type ~ ., scales="free_y")
# grid.arrange(p1,p2)

ggplot(f, aes(time, value, col=subtype)) + geom_point() + facet_grid(type ~ ., scales="free_y", switch = 'y')
ggsave("results.pdf")
ggsave("results.png")

subset = f[f$time + mn < ms, ]
ggplot(subset, aes(time, value, col=subtype)) + geom_point() + facet_grid(type ~ ., scales="free_y", switch = 'y')
ggsave("results-sub.pdf")
ggsave("results-sub.png")
