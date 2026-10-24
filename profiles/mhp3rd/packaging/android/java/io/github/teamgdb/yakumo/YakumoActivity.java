package io.github.teamgdb.yakumo;

import android.app.AlertDialog;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.database.Cursor;
import android.graphics.Insets;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.util.DisplayMetrics;
import android.util.TypedValue;
import android.view.ContextThemeWrapper;
import android.view.DisplayCutout;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import org.libsdl.app.SDLActivity;

import java.util.ArrayList;

/**
 * SDL's activity with what the host needs from Android and SDL does not
 * offer: the display cutout alone (SDL's safe area also counts the gesture
 * areas of hidden system bars), and the system's document picker for whole
 * folders, with the documents in them read and written through file
 * descriptors. The native side calls the static methods through JNI from the
 * game's thread; each picker call blocks that thread until the player has
 * chosen, never the UI thread.
 */
public class YakumoActivity extends SDLActivity {
    private static final int kPickTree = 0x59414b01;
    private static final Object sPickLock = new Object();
    private static boolean sPickDone;
    private static String sPickResult;

    /**
     * Lets the window cover a display cutout, whichever side it is on. The
     * theme asks for the short edges already; this makes sure of it, and
     * from Android 11 asks for every edge. SDL asks for the same when it
     * goes full screen, but only by changing the window's attributes without
     * applying them, so whether it took hold depended on a later change:
     * without it Android keeps the window beside the cutout and shows a black
     * strip there, on the left with the phone turned one way (#170).
     */
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        WindowManager.LayoutParams attributes = getWindow().getAttributes();
        attributes.layoutInDisplayCutoutMode = Build.VERSION.SDK_INT >= 30
            ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
            : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        getWindow().setAttributes(attributes);
    }

    /**
     * Keeps the game in landscape, either way up, as the manifest asks. SDL
     * calls this when it makes the window and would otherwise ask for every
     * orientation (FULL_USER) for a resizable window without an orientations
     * hint: the game then turned to portrait whenever the phone was held
     * upright, and stayed there on a phone with auto-rotate off.
     */
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    /**
     * Shows an SDL message box (the fatal error dialog, the setup dialogs)
     * with its text scrolling above buttons that always stay on screen.
     * SDL's own dialog puts the text and a row of buttons in one view that
     * does not scroll: on a landscape phone a long error pushed the buttons
     * to the bottom edge, squashed until their labels could not be read.
     * Here the text scrolls in whatever height is left, the buttons keep
     * their full size, and they stack when they do not fit side by side.
     * SDL calls this on the UI thread; the button pressed goes into
     * messageboxSelection before the dialog is dismissed, and SDL's dismiss
     * listener wakes the waiting thread, as in SDL's version. The box's
     * colors (SDL_MessageBoxColorScheme) are not used; no caller sets them.
     */
    @Override
    protected void messageboxCreateAndShow(Bundle args) {
        final Context themed = new ContextThemeWrapper(this, android.R.style.Theme_DeviceDefault_Dialog_Alert);
        final DisplayMetrics metrics = getResources().getDisplayMetrics();
        final int[] buttonFlags = args.getIntArray("buttonFlags");
        final int[] buttonIds = args.getIntArray("buttonIds");
        final String[] buttonTexts = args.getStringArray("buttonTexts");

        final AlertDialog dialog = new AlertDialog.Builder(themed).create();
        dialog.setTitle(args.getString("title"));
        dialog.setCancelable(false);
        dialog.setOnDismissListener(unused -> {
            synchronized (messageboxSelection) {
                messageboxSelection.notify();
            }
        });

        TextView message = new TextView(themed);
        message.setText(args.getString("message"));
        message.setTextAppearance(android.R.style.TextAppearance_Material_Subhead);
        message.setPadding(dp(24), dp(8), dp(24), dp(8));
        ScrollView scroll = new ScrollView(themed);
        scroll.addView(message);

        // SDL has put the buttons in the order they are shown.
        ButtonBar bar = new ButtonBar(themed);
        bar.setPadding(dp(12), dp(4), dp(12), dp(8));
        Button enter = null;
        Button escape = null;
        for (int i = 0; i < buttonTexts.length; ++i) {
            Button button = new Button(themed, null, android.R.attr.buttonBarButtonStyle);
            button.setText(buttonTexts[i]);
            button.setAllCaps(false);
            button.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
            button.setMinHeight(dp(48));
            final int id = buttonIds[i];
            button.setOnClickListener(v -> {
                messageboxSelection[0] = id;
                dialog.dismiss();
            });
            // SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT and _ESCAPEKEY_DEFAULT.
            if ((buttonFlags[i] & 0x1) != 0) enter = button;
            if ((buttonFlags[i] & 0x2) != 0) escape = button;
            bar.addView(button);
        }

        LinearLayout content = new LinearLayout(themed);
        content.setOrientation(LinearLayout.VERTICAL);
        // The text takes what height the title and the buttons leave, and
        // scrolls in it.
        content.addView(scroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1.0f));
        content.addView(bar, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT));
        dialog.setView(content);

        final Button enterButton = enter;
        final Button escapeButton = escape;
        dialog.setOnKeyListener((d, keyCode, event) -> {
            Button button = null;
            if (keyCode == KeyEvent.KEYCODE_ENTER) button = enterButton;
            // Back is the phone's Escape.
            if (keyCode == KeyEvent.KEYCODE_ESCAPE || keyCode == KeyEvent.KEYCODE_BACK) button = escapeButton;
            if (button == null) return false;
            if (event.getAction() == KeyEvent.ACTION_UP) button.performClick();
            return true;
        });

        dialog.show();
        // Wide enough in landscape that the text needs few lines, but not
        // across the whole of a wide phone.
        int width = Math.min(metrics.widthPixels - dp(32), dp(640));
        dialog.getWindow().setLayout(width, ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    /**
     * A row of buttons, right-aligned, that turns into a column of
     * full-width buttons when the row would not fit, so no label is cut.
     */
    private static final class ButtonBar extends LinearLayout {
        ButtonBar(Context context) {
            super(context);
            setOrientation(HORIZONTAL);
            setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
        }

        @Override
        protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
            int available = MeasureSpec.getSize(widthMeasureSpec) - getPaddingLeft() - getPaddingRight();
            int wanted = 0;
            int unspecified = MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED);
            for (int i = 0; i < getChildCount(); ++i) {
                View child = getChildAt(i);
                child.measure(unspecified, unspecified);
                wanted += child.getMeasuredWidth();
            }
            boolean stack = MeasureSpec.getMode(widthMeasureSpec) != MeasureSpec.UNSPECIFIED && wanted > available;
            setOrientation(stack ? VERTICAL : HORIZONTAL);
            for (int i = 0; i < getChildCount(); ++i) {
                LayoutParams params = (LayoutParams) getChildAt(i).getLayoutParams();
                params.width = stack ? ViewGroup.LayoutParams.MATCH_PARENT : ViewGroup.LayoutParams.WRAP_CONTENT;
            }
            super.onMeasure(widthMeasureSpec, heightMeasureSpec);
        }
    }

    /** Left, top, right, bottom of the display cutout in window pixels, or all 0. */
    public static int[] cutoutInsets() {
        int[] result = new int[4];
        if (mSingleton == null) return result;
        View view = mSingleton.getWindow().getDecorView();
        WindowInsets insets = view.getRootWindowInsets();
        if (insets == null) return result;
        if (Build.VERSION.SDK_INT >= 30) {
            Insets cutout = insets.getInsets(WindowInsets.Type.displayCutout());
            result[0] = cutout.left;
            result[1] = cutout.top;
            result[2] = cutout.right;
            result[3] = cutout.bottom;
        } else {
            // Android 10: the same insets through the older call.
            DisplayCutout cutout = insets.getDisplayCutout();
            if (cutout == null) return result;
            result[0] = cutout.getSafeInsetLeft();
            result[1] = cutout.getSafeInsetTop();
            result[2] = cutout.getSafeInsetRight();
            result[3] = cutout.getSafeInsetBottom();
        }
        return result;
    }

    /**
     * A short vibration for a touch control's press, as the system gives for
     * a key, done on the UI thread. The system's touch feedback setting
     * decides whether it is felt; no permission is needed.
     */
    public static void hapticTick() {
        if (mSingleton == null) return;
        final View view = mSingleton.getWindow().getDecorView();
        view.post(new Runnable() {
            @Override
            public void run() {
                view.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            }
        });
    }

    /** Asks the player for a folder; its tree URI, or null when cancelled. */
    public static String pickFolder() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        return pick(intent);
    }

    /** Asks the player for a file to read; its document URI, or null when cancelled. */
    public static String pickDocument() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        // Disc images have no MIME type every provider agrees on.
        intent.setType("*/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        return pick(intent);
    }

    private static String pick(Intent intent) {
        if (mSingleton == null) return null;
        synchronized (sPickLock) {
            sPickDone = false;
            sPickResult = null;
        }
        mSingleton.runOnUiThread(() -> {
            try {
                mSingleton.startActivityForResult(intent, kPickTree);
            } catch (Exception e) {
                finishPick(null);
            }
        });
        synchronized (sPickLock) {
            while (!sPickDone) {
                try {
                    sPickLock.wait();
                } catch (InterruptedException e) {
                    return null;
                }
            }
            return sPickResult;
        }
    }

    private static void finishPick(String result) {
        synchronized (sPickLock) {
            sPickResult = result;
            sPickDone = true;
            sPickLock.notifyAll();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == kPickTree) {
            Uri uri = resultCode == RESULT_OK && data != null ? data.getData() : null;
            finishPick(uri != null ? uri.toString() : null);
            return;
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    /** Starts the app afresh: this process ends and a new one opens the activity. */
    public static void relaunch() {
        if (mSingleton == null) return;
        Intent launch = mSingleton.getPackageManager().getLaunchIntentForPackage(mSingleton.getPackageName());
        if (launch == null || launch.getComponent() == null) return;
        mSingleton.startActivity(Intent.makeRestartActivityTask(launch.getComponent()));
        Runtime.getRuntime().exit(0);
    }

    /** The document that stands for a picked tree itself. */
    public static String treeRoot(String treeUri) {
        Uri tree = Uri.parse(treeUri);
        return DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))
            .toString();
    }

    /**
     * The children of a folder document in a picked tree, as "d/name/uri" for
     * folders and "f/name/uri" for files (names cannot hold '/').
     */
    public static String[] listFolder(String folderUri) {
        ArrayList<String> entries = new ArrayList<>();
        Uri folder = Uri.parse(folderUri);
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(folder,
            DocumentsContract.getDocumentId(folder));
        ContentResolver resolver = mSingleton.getContentResolver();
        String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                            DocumentsContract.Document.COLUMN_MIME_TYPE};
        try (Cursor cursor = resolver.query(children, columns, null, null, null)) {
            while (cursor != null && cursor.moveToNext()) {
                String id = cursor.getString(0);
                String name = cursor.getString(1);
                boolean directory = DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(2));
                Uri child = DocumentsContract.buildDocumentUriUsingTree(folder, id);
                entries.add((directory ? "d/" : "f/") + name + "/" + child);
            }
        } catch (Exception e) {
            return null;
        }
        return entries.toArray(new String[0]);
    }

    /** Creates a folder ("vnd.android.document/directory") or a file in a folder document. */
    public static String create(String folderUri, String name, boolean directory) {
        try {
            Uri created = DocumentsContract.createDocument(mSingleton.getContentResolver(), Uri.parse(folderUri),
                directory ? DocumentsContract.Document.MIME_TYPE_DIR : "application/octet-stream", name);
            return created != null ? created.toString() : null;
        } catch (Exception e) {
            return null;
        }
    }

    /** A file descriptor the caller owns for a document, mode "r" or "w", or -1. */
    public static int openDocument(String uri, String mode) {
        try {
            ParcelFileDescriptor descriptor =
                mSingleton.getContentResolver().openFileDescriptor(Uri.parse(uri), mode.equals("w") ? "wt" : mode);
            return descriptor != null ? descriptor.detachFd() : -1;
        } catch (Exception e) {
            return -1;
        }
    }
}
