"""Shared matplotlib configuration for all visualizers

This module sets up matplotlib in two stages:
1. Backend configuration (at module import) - must happen before importing pyplot
2. rcParams configuration (when calling configure_matplotlib) - must happen after pyplot import but before creating subplots
"""

import matplotlib

# Stage 1: Set backend at module level (happens on import, before pyplot)
matplotlib.use('WebAgg')


def cleanup_visualization():
    """
    Properly clean up WebAgg server state between visualizations.

    This function performs complete cleanup in the correct order:
    1. Destroys all figure managers (closes WebSocket connections)
    2. Stops the tornado IOLoop if running
    3. Resets WebAggApplication state variables
    4. Clears the IOLoop instance to force fresh creation next time
    5. Closes any remaining figures

    This ensures each new visualization starts with a clean WebAgg server.
    """
    try:
        import matplotlib.pyplot as plt
        from matplotlib._pylab_helpers import Gcf
        import time

        # Step 1: Destroy figure managers first (this closes WebSocket connections)
        Gcf.destroy_all()

        # Step 2: Stop tornado IOLoop and reset WebAggApplication state
        try:
            import tornado.ioloop
            from matplotlib.backends.backend_webagg import WebAggApplication

            # Get current IOLoop instance
            ioloop = tornado.ioloop.IOLoop.current(instance=False)
            if ioloop is not None:
                # Stop the IOLoop if it's running
                try:
                    if ioloop.asyncio_loop.is_running():
                        ioloop.stop()
                        # Wait for IOLoop to fully stop
                        time.sleep(0.3)
                except Exception:
                    pass  # IOLoop wasn't running or already stopped

                # Clear the IOLoop instance
                tornado.ioloop.IOLoop.clear_instance()

            # Reset WebAggApplication state flags
            WebAggApplication.started = False
            WebAggApplication.initialized = False

        except ImportError:
            pass  # tornado not available, skip IOLoop cleanup
        except Exception as e:
            import warnings
            warnings.warn(f"Error stopping tornado IOLoop: {e}")

        # Step 3: Close any remaining figures
        plt.close('all')

    except Exception as e:
        import warnings
        warnings.warn(f"Error during visualization cleanup: {e}")


def configure_matplotlib(port=8988):
    """
    Configure matplotlib rcParams for WebAgg and disable toolbar

    IMPORTANT: Must be called AFTER importing matplotlib.pyplot but BEFORE creating any figures/subplots

    Args:
        port: WebAgg server port (default: 8988)
    """
    # Stage 2: Set rcParams (happens after pyplot import, before subplots)
    matplotlib.rcParams['webagg.open_in_browser'] = True
    matplotlib.rcParams['webagg.port'] = port
    matplotlib.rcParams['webagg.port_retries'] = 50
    matplotlib.rcParams['toolbar'] = 'None'

    # WebAgg-specific: Remove toolbar items from client-side rendering
    # WebAgg sends toolbar items to JavaScript regardless of rcParams,
    # so we must patch the toolbar class to send empty items
    _patch_webagg_toolbar()

    # Suppress the "Press Ctrl+C to stop WebAgg server" message
    _suppress_webagg_messages()


def _suppress_webagg_messages():
    """
    Suppress WebAgg server status messages

    Patches WebAggApplication.start() to suppress the "Press Ctrl+C to stop WebAgg server"
    and "Server is stopped" messages, since we have our own user instructions.
    """
    try:
        from matplotlib.backends.backend_webagg import WebAggApplication
        import sys
        import io
        import signal
        from contextlib import contextmanager

        original_start = WebAggApplication.start

        @classmethod
        def silent_start(cls):
            """Modified start() that suppresses print messages"""
            import tornado.ioloop
            import asyncio

            try:
                asyncio.get_running_loop()
            except RuntimeError:
                pass
            else:
                cls.started = True

            if cls.started:
                return

            ioloop = tornado.ioloop.IOLoop.instance()

            def shutdown():
                ioloop.stop()
                # Suppress "Server is stopped" message
                cls.started = False

            @contextmanager
            def catch_sigint():
                old_handler = signal.signal(
                    signal.SIGINT,
                    lambda sig, frame: ioloop.add_callback_from_signal(shutdown))
                try:
                    yield
                finally:
                    signal.signal(signal.SIGINT, old_handler)

            cls.started = True

            # Start IOLoop without printing status messages
            with catch_sigint():
                ioloop.start()

        WebAggApplication.start = silent_start

    except Exception as e:
        import warnings
        warnings.warn(f"Could not suppress WebAgg messages: {e}")


def _patch_webagg_toolbar():
    """
    Patch WebAgg to hide the toolbar and file format dropdown

    WebAgg renders the toolbar and export dropdown client-side in the browser,
    so Python rcParams don't control them. We must:
    1. Remove toolbar buttons by emptying toolitems
    2. Remove file format dropdown by returning empty filetypes
    3. Inject CSS to hide the empty dropdown element that still gets created
    """
    try:
        from matplotlib.backends.backend_webagg_core import (
            NavigationToolbar2WebAgg,
            FigureCanvasWebAggCore,
            FigureManagerWebAgg
        )

        # 1. Remove toolbar buttons
        NavigationToolbar2WebAgg.toolitems = ()

        # 2. Remove file format dropdown by returning empty filetypes
        @classmethod
        def empty_filetypes(cls):
            return {}  # Empty dict = no file format options

        FigureCanvasWebAggCore.get_supported_filetypes_grouped = empty_filetypes

        # 3. Inject CSS to hide the empty dropdown element
        # JavaScript still creates the <select> element even when empty,
        # so we inject CSS to hide it
        original_get_javascript = FigureManagerWebAgg.get_javascript

        @classmethod
        def get_javascript_with_css(cls, stream=None):
            js_content = original_get_javascript(stream)

            # Inject CSS to hide toolbar elements
            css_injection = """
  (function() {
        var style = document.createElement('style');
        style.textContent = '.mpl-toolbar { display: none !important; }';
        document.head.appendChild(style);
    })();
            """

            # Prepend CSS to the JavaScript
            if isinstance(js_content, str):
                js_content = css_injection + js_content

            return js_content

        FigureManagerWebAgg.get_javascript = get_javascript_with_css

    except Exception as e:
        # Silently fail if patching doesn't work - toolbar will just be visible
        import warnings
        warnings.warn(f"Could not patch WebAgg toolbar: {e}")
