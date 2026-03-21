# check-link: Link all binaries (main + tests)

.PHONY: check-link

ALL_BINARIES = bin/example $(UNIT_TEST_BINARIES)

check-link:
ifdef FILE
	@rm -f $(FILE) 2>/dev/null; \
	output=$$($(MAKE) -s $(FILE) 2>&1); \
	status=$$?; \
	if [ $$status -eq 0 ]; then \
		echo "🟢 $(FILE)"; \
	else \
		echo "$$output" | grep -E "undefined reference|multiple definition|cannot find" | \
			sed "s|$$(pwd)/||g" | while read line; do \
			echo "🔴 $$line"; \
		done; \
	fi; \
	exit $$status
else ifdef RAW
	$(MAKE) -k -j$(MAKE_JOBS) $(ALL_OBJECTS)
	$(MAKE) -k -j$(MAKE_JOBS) $(ALL_BINARIES)
else
	@# Phase 1: Compile all objects in parallel
	@$(MAKE) -k -j$(MAKE_JOBS) $(ALL_OBJECTS) 2>&1 | grep -E "^(🟢|🔴)" || true
	@# Phase 2: Link all binaries in parallel
	@$(MAKE) -k -j$(MAKE_JOBS) $(ALL_BINARIES) 2>&1 | grep -E "^(🟢|🔴)" || true; \
	failed=0; \
	for bin in $(ALL_BINARIES); do \
		[ ! -f "$$bin" ] && failed=$$((failed + 1)); \
	done; \
	if [ $$failed -eq 0 ]; then \
		echo "✅ All binaries linked"; \
	else \
		echo "❌ $$failed binaries failed to link"; \
		exit 1; \
	fi
endif
