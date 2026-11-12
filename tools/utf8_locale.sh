# Source this from a script that invokes Guile and compares Unicode text.
# Guile encodes non-ASCII output according to the locale; the gate must not
# depend on the caller's environment. NPL_TEST_LC_ALL overrides the default.
if [ -n "${NPL_TEST_LC_ALL:-}" ]; then
    export LC_ALL="$NPL_TEST_LC_ALL"
else
    case "$(uname -s)" in
        Darwin) export LC_ALL=en_US.UTF-8 ;;
        *) export LC_ALL=C.UTF-8 ;;
    esac
fi
