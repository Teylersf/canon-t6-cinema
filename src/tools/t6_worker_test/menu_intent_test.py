#!/usr/bin/env python3
"""Compile the actual staged T6 menu request/consumer code, without a camera."""
import argparse
import importlib.util
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("t6_builder", HERE.parent / "build-t6.py")
BUILDER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILDER)


def between(text, start, end):
    assert text.count(start) == text.count(end) == 1
    return text.split(start, 1)[1].split(end, 1)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ml-source", required=True, type=Path)
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    compiler = shutil.which("cc") or shutil.which("gcc")
    assert compiler, "A host C compiler is required"
    with tempfile.TemporaryDirectory(prefix="t6-menu-test-") as directory:
        stage = Path(directory)
        (stage / "src").mkdir()
        for name in ("menu.c", "gui-common.c"):
            data = subprocess.check_output([
                "git", "-C", str(args.ml_source), "show", BUILDER.ML_PIN + ":src/" + name
            ])
            (stage / "src" / name).write_bytes(data)
        BUILDER.patch_retro_ui(stage)
        text = (stage / "src/menu.c").read_text()
        helper = between(text, "/* T6 menu intent helper begin: also compiled by the host scheduling test. */",
                         "/* T6 menu intent helper end. */")
        consumer = between(text, "/* T6 menu intent consumer begin. */",
                           "/* T6 menu intent consumer end. */")
        assert len(re.findall(r"give_semaphore\(\s*gui_sem\s*\)", text)) == 1
        assert text.count("t6_menu_request(T6_MENU_CLOSE);") == 3
        assert text.count("t6_menu_request(T6_MENU_OPEN);") == 1
        assert text.count("t6_menu_request(T6_MENU_TOGGLE);") == 4
        assert text.count("int desired = t6_menu_desired_state;") == 2
        assert "//~ TASK_CREATE( \"menu_task_minimal\"" in text
        runner = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"menu intent failure line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static int menu_shown, shooting_mode=17, initial_mode, irq_off, wakes, opens, closes;
static int semaphore;
static int *gui_sem=&semaphore;
static void (*after_restore)(void), (*during_open)(void), (*during_close)(void);
static uint32_t cli(void) { uint32_t old=(uint32_t)irq_off; irq_off=1; return old; }
static void sei(uint32_t old) { void(*hook)(void); irq_off=(int)old; if(!irq_off && after_restore) {hook=after_restore;after_restore=NULL;hook();} }
static void give_semaphore(int *s) { CHECK(s==gui_sem && !irq_off); wakes++; }
/* ACTUAL_HELPER */
static void menu_open(void) { void(*hook)(void); CHECK(!menu_shown); opens++; menu_shown=1; if(during_open){hook=during_open;during_open=NULL;hook();} }
static void menu_close(void) { void(*hook)(void); CHECK(menu_shown); closes++; menu_shown=0; if(during_close){hook=during_close;during_close=NULL;hook();} }
static void consume(void) { CHECK(wakes>0 && !irq_off); wakes--; /* ACTUAL_CONSUMER */ }
static void drain(void) { int n=0; while(wakes) {CHECK(n++<20);consume();} CHECK(!irq_off); }
static void want_open(void) {t6_menu_request(T6_MENU_OPEN);}
static void want_close(void) {t6_menu_request(T6_MENU_CLOSE);}
static void reset(void) { CHECK(!irq_off);menu_shown=0;t6_menu_desired_state=0;wakes=opens=closes=initial_mode=0;after_restore=during_open=during_close=NULL; }
static void opened(void) {reset();want_open();drain();CHECK(menu_shown && opens==1 && initial_mode==shooting_mode);}
static void earlier_wake_closes(void) {CHECK(wakes==1);consume();CHECK(!menu_shown);}
static void earlier_wake_then_reopen(void) {earlier_wake_closes();want_open();drain();CHECK(menu_shown);}
int main(void) {
    reset();want_close();CHECK(!wakes);want_open();want_open();CHECK(wakes==1);drain();CHECK(menu_shown && opens==1);
    /* Native half key and redraw fallback both close, including reverse order. */
    opened();want_close();want_close();CHECK(wakes==1);drain();CHECK(!menu_shown && closes==1);
    opened();want_close();drain();want_close();CHECK(!wakes && !menu_shown);
    /* Old already queued wake consumes a close intent before its giver posts.
     * The delayed post applies the same closed state and must not reopen. */
    opened();wakes=1;after_restore=earlier_wake_closes;want_close();CHECK(wakes==1 && !menu_shown);drain();CHECK(!menu_shown && closes==1);
    /* An explicit newer OPEN wins even when the older close giver posts last. */
    opened();wakes=1;after_restore=earlier_wake_then_reopen;want_close();CHECK(menu_shown && wakes==1);drain();CHECK(menu_shown && opens==2 && closes==1);
    /* Open followed by close before either wake, and two toggles, stay closed. */
    reset();want_open();want_close();CHECK(wakes==2);drain();CHECK(!menu_shown && !opens && !closes);
    reset();t6_menu_request(T6_MENU_TOGGLE);t6_menu_request(T6_MENU_TOGGLE);CHECK(wakes==2);drain();CHECK(!menu_shown && !opens);
    opened();t6_menu_request(T6_MENU_TOGGLE);t6_menu_request(T6_MENU_TOGGLE);drain();CHECK(menu_shown && opens==1 && !closes);
    /* Requests arriving while slow Canon open/close executes are not reset. */
    reset();during_open=want_close;want_open();drain();CHECK(!menu_shown && opens==1 && closes==1 && t6_menu_desired_state==0);
    opened();during_close=want_open;want_close();drain();CHECK(menu_shown && opens==2 && closes==1 && t6_menu_desired_state==1);
    /* Preserve pre-existing interrupt state and never call DryOS under IRQ lock. */
    reset();irq_off=1;t6_menu_request(T6_MENU_CLOSE);CHECK(irq_off==1 && !wakes);irq_off=0;
    puts("T6 actual staged menu intents: producer audit and scheduling regressions passed");
    return 0;
}
'''.replace("/* ACTUAL_HELPER */", helper).replace("/* ACTUAL_CONSUMER */", consumer)
        source = stage / "menu_intent_test.c"
        binary = stage / "menu_intent_test"
        source.write_text(runner)
        flags = ["-std=c99", "-g", "-Wall", "-Wextra", "-Werror", "-O2"]
        if args.sanitize:
            flags += ["-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                      "-fno-omit-frame-pointer"]
        subprocess.run([compiler, *flags, str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
