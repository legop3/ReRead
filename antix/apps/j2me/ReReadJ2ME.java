import java.awt.KeyEventDispatcher;
import java.awt.KeyboardFocusManager;
import java.awt.event.KeyEvent;
import java.lang.reflect.Method;
import org.recompile.mobile.Mobile;
import java.util.jar.Attributes;
import java.util.jar.JarFile;

/**
 * Preserve FreeJ2ME-Plus' unmodified AWT frontend while translating ReRead's
 * physical keys to the desktop keys FreeJ2ME already understands.
 */
public final class ReReadJ2ME {
    private static boolean guideHeld = false;
    private ReReadJ2ME() {}

    public static void main(String[] args) throws Exception {
        KeyboardFocusManager.getCurrentKeyboardFocusManager().addKeyEventDispatcher(new KeyEventDispatcher() {
            @Override public boolean dispatchKeyEvent(KeyEvent event) {
                if (event.getID() != KeyEvent.KEY_PRESSED && event.getID() != KeyEvent.KEY_RELEASED)
                    return false;
                // ? + Back quits even while FreeJ2ME owns focus.
                if (event.getKeyCode() == KeyEvent.VK_F1) {
                    guideHeld = event.getID() == KeyEvent.KEY_PRESSED;
                    return true;
                }
                if (guideHeld && event.getKeyCode() == KeyEvent.VK_ESCAPE) {
                    if (event.getID() == KeyEvent.KEY_PRESSED) System.exit(0);
                    return true;
                }
                int to = 0;
                switch (event.getKeyCode()) {
                    case KeyEvent.VK_ESCAPE: to = KeyEvent.VK_W; break;      // Back -> right softkey
                    case KeyEvent.VK_CONTEXT_MENU: to = KeyEvent.VK_Q; break;// Options -> left softkey
                    case KeyEvent.VK_ADD: to = KeyEvent.VK_E; break;        // Zoom+ -> *
                    case KeyEvent.VK_SUBTRACT: to = KeyEvent.VK_R; break;   // Zoom- -> #
                    default: break; // Arrows and Enter are mapped natively
                }
                if (to != 0) {
                    event.setKeyCode(to);
                    event.setKeyChar(Character.toLowerCase((char)to));
                }
                return false;
            }
        });
        final String jar = System.getProperty("reread.freej2me.jar");
        if (jar == null) throw new IllegalArgumentException("Missing -Dreread.freej2me.jar");
        String mainClass;
        try (JarFile input = new JarFile(jar)) {
            Attributes attrs = input.getManifest().getMainAttributes();
            mainClass = attrs.getValue(Attributes.Name.MAIN_CLASS);
        }
        if (mainClass == null) throw new IllegalArgumentException("FreeJ2ME JAR missing Main-Class");
        Method entry = Class.forName(mainClass).getMethod("main", String[].class);
        entry.invoke(null, (Object) args);
        // Apply actual LCD rotation through FreeJ2ME's own settings.
        String rotation = System.getProperty("reread.rotation", "0");
        if (!rotation.equals("0")) {
            Mobile.config.settings.put("rotate", rotation);
            Class<?> frontend = Class.forName(mainClass);
            Object app = frontend.getField("app").get(null);
            Method apply = frontend.getDeclaredMethod("settingsChanged", boolean.class);
            apply.setAccessible(true);
            apply.invoke(app, Boolean.FALSE);
        }
    }
}
