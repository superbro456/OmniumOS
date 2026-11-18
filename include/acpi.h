#ifndef ACPI_H
#define ACPI_H
#include <stdint.h>
/* APIC ID всех ядер CPU из ACPI MADT; 0 — таблицы нет */
int acpi_cpu_list(uint32_t *apic_ids, int max);
#endif
