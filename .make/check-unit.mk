# check-unit: Run unit tests and parse XML results

.PHONY: check-unit

check-unit:
ifdef FILE
	@# Single test mode - run one test binary with detailed output
	@mkdir -p reports/check/$$(dirname $(FILE) | sed 's|^$(BUILDDIR)/tests/||')
	@if [ ! -x "$(FILE)" ]; then \
		echo "🔴 $(FILE): binary not found (run make first)"; \
		exit 1; \
	fi; \
	$(FILE); \
	exit $$?
else ifdef RAW
	@# RAW mode - run tests with full output visible
	@$(MAKE) -k -j$(MAKE_JOBS) $(SRC_OBJECTS) $(TEST_OBJECTS) $(VENDOR_OBJECTS) 2>&1 | grep -E "^(🟢|🔴)" || true
	@$(MAKE) -k -j$(MAKE_JOBS) $(UNIT_TEST_BINARIES) 2>&1 | grep -E "^(🟢|🔴)" || true
	@mkdir -p reports/check/unit
	@for bin in $(UNIT_TEST_BINARIES); do \
		echo "=== $$bin ==="; \
		$$bin || exit 1; \
	done
else
	@# Bulk mode - build and run all unit tests
	@$(MAKE) -k -j$(MAKE_JOBS) $(SRC_OBJECTS) $(TEST_OBJECTS) $(VENDOR_OBJECTS) 2>&1 | grep -E "^(🟢|🔴)" || true
	@$(MAKE) -k -j$(MAKE_JOBS) $(UNIT_TEST_BINARIES) 2>&1 | grep -E "^(🟢|🔴)" || true
	@mkdir -p reports/check/unit
	@passed=0; failed=0; \
	for bin in $(UNIT_TEST_BINARIES); do \
		if [ ! -f "$$bin" ]; then \
			echo "🔴 $$bin (not built)"; \
			failed=$$((failed + 1)); \
		elif $$bin >/dev/null 2>&1; then \
			echo "🟢 $$bin"; \
			passed=$$((passed + 1)); \
		else \
			echo "🔴 $$bin"; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	total=$$((passed + failed)); \
	if [ $$failed -eq 0 ]; then \
		echo "✅ All $$total test binaries passed"; \
	else \
		echo "❌ $$failed/$$total test binaries failed"; \
		exit 1; \
	fi
endif
