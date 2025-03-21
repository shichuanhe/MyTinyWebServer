半同步/半异步（Half-Sync/Half-Async）工作方式：
    场景：餐厅点餐
    想象你经营了一家餐厅，客人们不断进来点餐，厨房需要高效地处理这些订单并制作食物。为了提升效率，你采用了一种 半同步/半异步（Half-Sync/Half-Async） 的工作方式：

    a.前台服务员（异步部分）：
        负责接待客人、记录订单（接收请求）。
        他们不会自己做菜，而是把订单放到“订单队列”里。
        他们可以同时处理多个客人（异步），不会因一个客人点餐而阻塞。
        后厨厨师（同步部分）：

    b.厨师们在厨房里按照订单制作食物（同步处理）：
        每个厨师一次只能做一道菜，处理是同步的，但有多个厨师可以并行处理不同的订单（多线程）。
        当一个订单完成后，厨师会通知前台服务员，告诉他们可以接待下一位客人了。

    c.订单队列（任务队列）：
        这是一个存放订单的地方，厨师们按照顺序从这里取订单并制作食物。

    这种方式的好处是：
        前台服务员可以同时接待多个客人，提高了餐厅的接待效率。
        后厨厨师可以并行处理不同的订单，加快了菜品的制作速度。
        订单队列可以确保订单的处理顺序，防止厨师处理错误的订单。
    这种方式的缺点是：
        当订单量很大时，订单队列可能会堆积，导致顾客等待时间变长。
        但在餐厅这个例子中，这种方式可以提高效率，因为前台服务员可以同时接待多个客人，而后厨厨师可以并行处理不同的订单。
    半同步/半异步工作方式在实际应用中非常常见，例如 Web 服务器、数据库服务器等。它可以提高服务器的并发处理能力，同时也能保证系统的稳定性和可靠性。  

同步线程池（Sync Thread Pool）：
    餐厅模型：传统小餐馆

    场景：餐厅只有一个厨师，客人点了菜，厨师必须做完才能接待下一位客人。
        前台服务员和后厨厨师是同一批人，他们既要接待客人，也要做菜。
        客人来了，服务员先记录订单，然后去厨房做菜。
        后厨厨师做完后，前台服务员再回来接待下一位客人。
    特点
        请求处理是同步的：必须等待当前任务完成，才能处理下一个。
        线程数固定，任务多了就要等待。
        适用于请求量较小的情况，否则服务器容易被阻塞，降低并发能力。
    问题
        如果一个客人点了一道复杂的菜，整个餐厅可能都会因为这个订单而变慢。

异步线程池（Async Thread Pool）:
    餐厅模型：快餐店
    场景：餐厅有多个服务员，客人点了菜，服务员会把订单放到“订单队列”里，然后去前台取餐。
        前台服务员和后厨厨师是不同的人，他们分工明确。
        客人来了，服务员先记录订单，然后去前台取餐。
        前台服务员会通知后厨厨师，告诉他们有新的订单。
        后厨厨师做完后，会通知前台服务员，告诉他们可以取餐了。
    特点
        完全异步：前台不会等待后厨处理完成，而是继续接待新的客人。
        请求处理是异步的：客人不需要等待当前任务完成，就可以去取餐。
        线程数可以根据实际情况动态调整，任务多了可以增加线程数，任务少了可以减少线程数。
        适用于请求量较大的情况，因为服务员可以同时接待多个客人，提高了餐厅的接待效率。
    问题
        由于请求处理是异步的，所以需要额外的机制来保证任务的顺序和结果的一致性。
        需要额外的通知机制，否则客人不知道什么时候取餐（类似于回调函数或 Future/Promise 机制）。


————————————————————————————————————————————————————————
特性	Reactor 模式（服务员主动检查）	Proactor 模式（厨房主动通知）
控制I/O操作的主体	服务员（线程）主动发起操作并检查进度。	厨房（服务器）主动处理请求并通知服务员（线程）。
处理方式	服务员等待厨房准备菜肴（阻塞等待）。	服务员等待厨房通知（非阻塞）。
工作效率	服务员需要不断地查看厨房状态，可能浪费时间等待。	厨房完成后直接通知服务员，避免了不必要的等待。
适用场景	适用于事件驱动的应用场景，需要主动控制每个事件的处理。	适用于高效处理 I/O 操作，减少线程等待的场景。